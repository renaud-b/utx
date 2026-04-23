#pragma once

#include <vector>
#include <cstdint>

namespace utx::domain::port {

    class ISymmetricCrypto {
    public:
        virtual ~ISymmetricCrypto() = default;

        [[nodiscard]]
        virtual std::vector<uint8_t>
        generate_key_256() const = 0;

        [[nodiscard]]
        virtual std::vector<uint8_t>
        encrypt(const std::vector<uint8_t>& plaintext,
                const std::vector<uint8_t>& key,
                std::vector<uint8_t>& out_iv) const = 0;

        [[nodiscard]]
        virtual std::vector<uint8_t>
        decrypt(const std::vector<uint8_t>& ciphertext,
                const std::vector<uint8_t>& key,
                const std::vector<uint8_t>& iv) const = 0;
    };

}