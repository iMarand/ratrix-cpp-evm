#include <iostream>
#include <fstream>
#include <functional>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

template <typename... Args>
void coutLn(Args&&... args) {
    (std::cout << ... << args) << "\n";
}

struct Person {
    Person(std::string name, int age) {
        coutLn(name, " ", age);
    }
};

int main() {
    Person::Person("Deo", 12);
  
    return 0;
}