/**
 * Author: Marand
 * Description: This program is for educational purpose, use it as guidline not to attack or brutforce other people's wallets
*/

#include <iostream>

#include "../RTX_LIBS.h"
#include "../.env"
#include "targets.h"

// Randomize For Random Private Key With 64 digits
std::string randomizeHex(uint_least16_t len) {
	std::random_device rand;
    std::mt19937 generator(rand()); 
    std::uniform_int_distribution<int> distribution(0, 15); 

    std::ostringstream randVal;

    for (int i = 0; i < len; ++i) {
        int k = distribution(generator);
        randVal << std::hex << std::setw(1) << std::setfill('0') << k;
    }

    return randVal.str();
};

// Match The Random Private Key With Its Address And Check From List
class MATCH_ADDRESS {
    public:
        std::string rand_address;
        std::string rand_privateKey;
        
        bool compareList() {
            for(int p = 0; p <= 10600; p++) {
                if (rand_address == targetAddresses[p]) {
                    return true;
                }
            }

            return false;
        };
};

// This Will Loop The Random Address To Match It To The Provided List
void BruteForceFromList(uint32_t leastNum) {
    MATCH_ADDRESS ADDRESS_OBJ;

    for(int measure = 0; measure < leastNum; measure++) {
        ADDRESS_OBJ.rand_privateKey = randomizeHex(64);
        ADDRESS_OBJ.rand_address = RTX::toAddress(randomizeHex(64));

        bool res = ADDRESS_OBJ.compareList();

        if(res == true) {
            coutLn("The Random Address Is: ", ADDRESS_OBJ.rand_address);
            coutLn("The Private Key Is: ", ADDRESS_OBJ.rand_privateKey);

            break;
        } else {
            coutLn("False Result Address: ", ADDRESS_OBJ.rand_address);
        }
    }

}

void TestProgramMatchList(int opt);

// Main Function Of The Program
auto main() -> int {
    BruteForceFromList(2000);

    return 0;
}

// Test Function To Test Real Address To Confirm Program If Works
void TestProgramMatchList(int opt) {
    MATCH_ADDRESS ADDRESS_OBJ;

    switch (opt) {
        case 1:
            ADDRESS_OBJ.rand_privateKey = randomizeHex(64);
            ADDRESS_OBJ.rand_address = RTX::toAddress(randomizeHex(64));

            break;
        case 2:
            ADDRESS_OBJ.rand_privateKey = _PRIVATE_KEY_;
            ADDRESS_OBJ.rand_address = _ADDRESS_;

            break;
        default:
           coutLn("Invalid Option");
    }


    bool res = ADDRESS_OBJ.compareList();

    if(res == true) {
        coutLn("The Random Address Is: ", ADDRESS_OBJ.rand_address);
        coutLn("The Private Key Is: ", ADDRESS_OBJ.rand_privateKey);
    } else {
        coutLn("False Result: ", res);
    }
}