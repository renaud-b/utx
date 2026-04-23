#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <memory>
#include <thread>

#include "SandboxedVm.hpp"
#include "VmInfraBinder.hpp"

namespace utx::infra::languages::javascript {
    /** Configuration for the VmPool.
     *  - size: Number of VM instances in the pool (default 4).
     *  - memory_bytes: Memory limit per VM in bytes (default 32MB).
     *  - timeout_ms: Execution timeout per VM in milliseconds (default 200ms).
     */
    struct VmPoolConfig {
        size_t  size          = 4;                      // nombre de VM
        size_t  memory_bytes  = 32 * 1024 * 1024;       // 32MB par VM
        int     timeout_ms    = 200;                    // 200ms par exécution
        constexpr VmPoolConfig() = default;
    };
    /** VmPool manages a pool of SandboxedVm instances,
     * allowing concurrent acquisition and release of VMs for executing JavaScript code
     * @wip we need to check for the multithreading part
     * @see DISABLED_VmPoolTest, HandlesConcurrentExecutionSafely
     */
    class VmPool final {
    public:
        /** Create a VmPool with the specified configuration.
         *  @param binder Optional VmInfraBinder to bind infrastructure to each VM.
         *  @param cfg The configuration for the pool.
         */
        explicit VmPool(const std::unique_ptr<VmInfraBinder> &binder, VmPoolConfig cfg = {}) : cfg_(std::move(cfg)) {
            for (size_t i = 0; i < cfg_.size; ++i) {
                SandboxedVm::Limits lims{ cfg_.memory_bytes, cfg_.timeout_ms };
                auto vm = std::make_unique<SandboxedVm>(lims);
                if (binder) {
                    binder->bind(*vm);
                }
                pool_.push(std::move(vm));
            }
        }
        /** Lease represents a leased SandboxedVm from the VmPool.
         * It automatically returns the VM to the pool when destroyed or moved.
         */
        class Lease {
        public:
            Lease(std::unique_ptr<SandboxedVm> vm, VmPool& owner)
            : vm_(std::move(vm)), owner_(&owner) {}
            Lease(Lease&& other) noexcept : vm_(std::move(other.vm_)), owner_(other.owner_) { other.owner_ = nullptr; }
            /** Move assignment operator */
            Lease& operator=(Lease&& other) noexcept {
                if (this != &other) {
                    release_();
                    vm_ = std::move(other.vm_);
                    owner_ = other.owner_; other.owner_ = nullptr;
                }
                return *this;
            }
            ~Lease() { release_(); }
            [[nodiscard]]
            SandboxedVm& vm() const { return *vm_; }
        private:
            /** Release the VM back to the pool if owned */
            void release_() {
                if (owner_ && vm_) {
                    std::lock_guard lk(owner_->mtx_);
                    owner_->pool_.push(std::move(vm_));
                    owner_->cv_.notify_one();
                }
            }
            std::unique_ptr<SandboxedVm> vm_;
            VmPool* owner_{nullptr};
        };
        /** Acquire a Lease on a SandboxedVm from the pool.
         *  Blocks if no VMs are currently available.
         *  @returns A Lease managing the acquired SandboxedVm.
         */
        [[nodiscard]]
        Lease acquire() {
            std::unique_lock lk(mtx_);
            cv_.wait(lk, [&]{ return !pool_.empty(); });
            auto vm = std::move(pool_.front());
            pool_.pop();
            return Lease(std::move(vm), *this);
        }

    private:
        VmPoolConfig cfg_;
        std::mutex mtx_;
        std::condition_variable cv_;
        std::queue<std::unique_ptr<SandboxedVm>> pool_;
    };
} // namespace utx::infra::vm::javascript
