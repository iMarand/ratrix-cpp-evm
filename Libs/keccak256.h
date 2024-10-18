
class Keccak256 {
private:
    static constexpr uint64_t keccakf_rndc[24] = {
        0x0000000000000001, 0x0000000000008082, 0x800000000000808a,
        0x8000000080008000, 0x000000000000808b, 0x0000000080000001,
        0x8000000080008081, 0x8000000000008009, 0x000000000000008a,
        0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
        0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
        0x8000000000008003, 0x8000000000008002, 0x8000000000000080,
        0x000000000000800a, 0x800000008000000a, 0x8000000080008081,
        0x8000000000008080, 0x0000000080000001, 0x8000000080008008
    };

    static constexpr int keccakf_rotc[24] = {
        1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
        27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
    };

    static constexpr int keccakf_piln[24] = {
        10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
    };

    std::array<uint64_t, 25> st;
    int byteIndex;
    int wordIndex;

    void keccakf(std::array<uint64_t, 25>& st) {
        uint64_t t, bc[5];
        for (int round = 0; round < 24; ++round) {
            // Theta
            for (int i = 0; i < 5; ++i)
                bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];

            for (int i = 0; i < 5; ++i) {
                t = bc[(i + 4) % 5] ^ ((bc[(i + 1) % 5] << 1) | (bc[(i + 1) % 5] >> 63));
                for (int j = 0; j < 25; j += 5)
                    st[j + i] ^= t;
            }

            // Rho Pi
            t = st[1];
            for (int i = 0; i < 24; ++i) {
                int j = keccakf_piln[i];
                bc[0] = st[j];
                st[j] = ((t << keccakf_rotc[i]) | (t >> (64 - keccakf_rotc[i])));
                t = bc[0];
            }

            // Chi
            for (int j = 0; j < 25; j += 5) {
                for (int i = 0; i < 5; ++i)
                    bc[i] = st[j + i];
                for (int i = 0; i < 5; ++i)
                    st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
            }

            // Iota
            st[0] ^= keccakf_rndc[round];
        }
    }

public:
    Keccak256() : byteIndex(0), wordIndex(0) {
        st.fill(0);
    }

    void update(const uint8_t* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            uint8_t byte = data[i];
            st[wordIndex] ^= static_cast<uint64_t>(byte) << (8 * byteIndex);
            if (++byteIndex == 8) {
                byteIndex = 0;
                if (++wordIndex == 17) {
                    keccakf(st);
                    wordIndex = 0;
                }
            }
        }
    }

    std::array<uint8_t, 32> finalize() {
        st[wordIndex] ^= static_cast<uint64_t>(0x01) << (8 * byteIndex);
        st[16] ^= 0x8000000000000000;
        keccakf(st);

        std::array<uint8_t, 32> hash;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 8; ++j) {
                hash[i * 8 + j] = (st[i] >> (8 * j)) & 0xFF;
            }
        }
        return hash;
    }

    static std::array<uint8_t, 32> hash(const std::string& input) {
        Keccak256 hasher;
        hasher.update(reinterpret_cast<const uint8_t*>(input.data()), input.length());
        return hasher.finalize();
    }
};

namespace kecca256 {
	auto getHex(std::string& hexInput) {
		std::string byteInput;
        for (size_t i = 0; i < hexInput.length(); i += 2) {
        	std::string byteString = hexInput.substr(i, 2);
            char byte = (char) strtol(byteString.c_str(), nullptr, 16);
            byteInput.push_back(byte);
        }

        // Compute Keccak256 hash
        std::array<uint8_t, 32> hash = Keccak256::hash(byteInput);

        // Convert hash to hexadecimal string
        std::stringstream ss;
        for (unsigned char byte : hash) {
        	ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
        }

        return ss.str();

    }

}

