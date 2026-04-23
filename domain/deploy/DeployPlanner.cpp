#include "DeployPlanner.hpp"

#include "common/Hash.hpp"
#include "common/Logger.hpp"
#include "domain/graph/GraphDiffer.hpp"
#include "domain/graph/Action.hpp"
#include "infrastructure/languages/html/HtmlParser.hpp"
#include "infrastructure/languages/javascript/JsParser.hpp"
#include "infrastructure/languages/css/CssParser.hpp"
#include "infrastructure/languages/cpp/CppParser.hpp"
#include "infrastructure/blackhole/BlackholeRebuilder.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "domain/model/AtomicBlock.hpp"
#include "domain/model/ChainConfig.hpp"
#include "infrastructure/network/Serialization.hpp"

using namespace utx::domain;
using namespace utx::domain::deploy;
using namespace utx::domain::graph;

namespace {

// ============================================================
// GENESIS
// ============================================================

std::string forge_genesis_payload(
    const model::Address &address,
    const std::vector<std::string> &projectors,
    const std::vector<std::string> &labels = {})
{
    model::ChainConfig config;
    config.owners.push_back(address);
    config.projectors = projectors;
    config.labels = labels;

    auto bytes = utx::domain::model::encode_msgpack(config);
    return utx::common::base64::encode(std::string(bytes.begin(), bytes.end()));
}

// ============================================================
// PARSER
// ============================================================

std::shared_ptr<GraphElement> parse_content(
    const std::string &content,
    const std::string &kind,
    const std::string &chain_id
) {
    if (kind == "html") {
        utx::infra::languages::html::HtmlParser parser;
        const auto actions = parser.parse(content);

        auto g = std::make_shared<Graph>(utx::common::UUID(chain_id));
        for (const auto &a: actions)
            utx::infra::blackhole::BlackholeRebuilder().apply_action(g, a);

        return g->root();
    }

    if (kind == "js") {
        utx::infra::languages::javascript::JsParser parser(false);
        return parser.parse(content);
    }

    if (kind == "css") {
        utx::infra::languages::css::CssParser parser;
        return parser.parse(content);
    }

    if (kind == "cpp") {
        utx::infra::languages::cpp::CppParser::Options opt;
        opt.emit_unnamed_tokens = true;
        opt.emit_ranges = false;
        opt.trim_whitespace_tokens = false;

        const auto actions =
            utx::infra::languages::cpp::CppParser::parse(content, opt);

        auto g = std::make_shared<Graph>(utx::common::UUID(chain_id));
        for (const auto &a: actions)
            utx::infra::blackhole::BlackholeRebuilder().apply_action(g, a);

        return g->root();
    }

    auto g = std::make_shared<Graph>(utx::common::UUID(chain_id));

    try {
        const auto j = nlohmann::json::parse(content);
        g->parse_from(j);
        return g->root();
    } catch (const std::exception &e) {
        LOG_THIS_ERROR("❌ Failed to parse graph content for chain {}: {}", chain_id, e.what());
        return nullptr;
    }
}

// ============================================================
// PROJECTOR RESOLUTION
// ============================================================

std::string resolve_projector(const std::string& kind, const std::string& fallback)
{
    if (kind == "js") return "JsProjector";
    if (kind == "html") return "WebProjector";
    if (kind == "css") return "CssProjector";
    if (kind == "cpp") return "CppProjector";
    if (kind == "graph") return "GraphProjector";
    return fallback;
}

} // namespace

// ============================================================
// PLAN
// ============================================================

std::expected<DeployPlan, std::string>
DeployPlanner::plan(const DeployPlanInput &input)
{
    // ============================================================
    // 1. PARSE
    // ============================================================

    auto local_root = parse_content(input.content, input.kind, input.chain_id);

    if (!local_root)
        return std::unexpected("Failed to parse content.");

    auto local_graph = std::make_shared<Graph>(
        utx::common::UUID(input.chain_id),
        local_root
    );

    // ============================================================
    // 2. DIFF
    // ============================================================

    std::vector<Action> diff;

    if (!input.remote_graph.has_value()) {
        utx::infra::blackhole::GraphActionEmitter::emit_under_parent(
            local_root,
            local_root->id(),
            local_root->path(),
            diff
        );
    } else {
        diff = GraphDiffer::compute_diff(
            input.remote_graph->root(),
            local_root
        );
    }

    // ============================================================
    // 3. BUILD SNAPSHOT
    // ============================================================

    const std::string snapshot_b64 =
        utx::common::base64::encode(local_graph->to_json_string());

    const std::string snapshot_payload =
        "urn:pi:graph:snap:" + snapshot_b64;

    const size_t snapshot_size = snapshot_payload.size();

    // ============================================================
    // 4. BUILD ACTIONS
    // ============================================================

    std::vector<std::string> action_payloads;

    if (!diff.empty()) {
        auto grouped = batch_into_group_actions(diff, input.group_action_size);

        for (const auto &g: grouped) {
            action_payloads.push_back(
                "urn:pi:graph:action:" + g.encode()
            );
        }
    }

    size_t incremental_size = 0;
    for (const auto& a : action_payloads)
        incremental_size += a.size();

    // ============================================================
    // 5. STRATEGY
    // ============================================================

    bool is_snapshot;

    if (input.force_snapshot)
        is_snapshot = true;
    else if (input.force_group_actions)
        is_snapshot = false;
    else
        is_snapshot = snapshot_size < incremental_size;

    // ============================================================
    // 6. BUILD TRANSACTIONS (ORDERED)
    // ============================================================

    std::vector<PlannedTransaction> txs;

    // ---------- GENESIS ----------
    if (input.is_new_chain) {

        LOG_THIS_INFO("🔨 {} is a new chain → adding GENESIS first", input.chain_id);

        const auto projector = resolve_projector(input.kind, input.projector);

        txs.push_back({
            forge_genesis_payload(
                model::Address(input.sender_address),
                {projector}
            )
        });
    }

    // ---------- CONTENT ----------
    if (is_snapshot) {

        LOG_THIS_INFO("📦 Using SNAPSHOT strategy for chain {} with snapshot size {} bytes ({} actions with total size {} bytes)",
                      input.chain_id, snapshot_size, action_payloads.size(), incremental_size);

        txs.push_back({snapshot_payload});

    } else {

        LOG_THIS_INFO("⚡ Using INCREMENTAL strategy with {} actions with total size {} bytes (snapshot size would be {} bytes) for chain {}",
                      action_payloads.size(), incremental_size, snapshot_size, input.chain_id);

        for (const auto& payload : action_payloads)
            txs.push_back({payload});
    }

    // ---------- COMMIT ----------
    const auto commit_payload = CommitPayload{
        .message = input.commit_message,
        .author = input.sender_address,
    };

    const auto commit_action = build_commit_action(commit_payload);

    txs.push_back({
        "urn:pi:graph:action:" + commit_action.encode()
    });

    // ============================================================
    // 7. CONTENT HASH
    // ============================================================

    const std::string content_hash =
        utx::common::md5_hex(input.content);

    // ============================================================
    // 8. PLAN ID (DETERMINISTIC)
    // ============================================================

    std::string accumulator =
        input.chain_id +
        content_hash +
        (is_snapshot ? "snapshot" : "incremental");

    for (const auto &tx: txs)
        accumulator += tx.payload_data;

    const std::string plan_id =
        common::sha256_hex(accumulator);

    // ============================================================
    // 9. SUMMARY
    // ============================================================

    std::string summary =
        input.file_path +
        " → " +
        (is_snapshot ? "SNAPSHOT" : "INCREMENTAL") +
        " (" +
        std::to_string(txs.size()) +
        " tx)";

    // ============================================================
    // 10. BUILD PLAN
    // ============================================================

    DeployPlan plan;
    plan.plan_id = plan_id;
    plan.strategy = is_snapshot ? "snapshot" : "incremental";
    plan.transactions = std::move(txs);
    plan.content_hash = content_hash;
    plan.summary = summary;

    LOG_THIS_DEBUG("Plan : {}", plan.to_string());

    return plan;
}
