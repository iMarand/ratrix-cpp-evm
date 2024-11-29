#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include <fstream>
#include "pk-address.h"
#include "words.h"

using namespace std;

typedef struct {
	int e = 500;
	int k_length = 10723;
	
} INFO;
struct G_P {
string gRandom(int len) {
	std::random_device rd;  
    std::mt19937 generator(rd()); 
    std::uniform_int_distribution<int> distribution(0, 15); 

    std::ostringstream hs;

    for (int i = 0; i < len; ++i) {
      int rh = distribution(generator);
        hs << std::hex << std::setw(1) << std::setfill('0') << rh;
    }
    printf("%s\n", hs.str().c_str());

    return hs.str();
}

string EVM_address(string pk) {
	std::string address = eth::getAddress(pk);
	
	return address;
}

string BF_address(int l) {
	int i = 0;
	
	do {
	
		string const &g_ = EVM_address(gRandom(64));
		
		for(int c=0; c<=10600; c++) {
			string cr(k_address_[c]);
			if(g_ == cr) {
				ofstream file("data.dat", ios::app);
				
				file << gRandom(64) << "\n->";
				file << g_ << "\n";
				file << "_________" << "\n";
				
				printf("%s\n", gRandom(64).c_str());
				printf("%s\n", g_.c_str());
			}
		};
			
		// printf("%s\n", g_.c_str());
		
		i++;
	} while(i < l);
	
	return "";
}

string CK_address(uint64_t l) {
	int i = 0;
	
	do {
		i++;
	} while(i < l);
	
	return "";
}
	
};

int main() {
	INFO _;
	G_P __;
	
    cout << __.BF_address(1000000);
    return 0;
}
