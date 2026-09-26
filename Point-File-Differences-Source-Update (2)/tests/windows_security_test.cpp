#include "point_compliance.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
int main() {
    const auto root=std::filesystem::temp_directory_path()/L"point-windows-security-test";
    std::filesystem::create_directories(root);
    auto policy=[&](const char* enforce,const char* retention) {
        std::ofstream out(root/"point-security.conf");
        out<<"enforce_windows_groups="<<enforce<<"\nallowed_windows_groups=Point Users\nexport_windows_groups=Point Exporters\nexport_retention_days="<<retention<<"\nworkspace_retention_days=30\nlog_retention_days=365\n";
    };
    auto read_policy = [&]() {
        std::ifstream input(root/"point-security.conf");
        const std::string text{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        return point::compliance::parse_policy_text(text);
    };
    policy("true","30");assert(read_policy().enforce_windows_groups);
    for(const auto* value:{"tru","false",""}) {
        policy(value,"30");bool denied=false;try{read_policy();}catch(...){denied=true;}assert(denied);
    }
    policy("true","30days");bool denied=false;try{read_policy();}catch(...){denied=true;}assert(denied);
    policy("true","30");{std::ofstream out(root/"point-security.conf",std::ios::app);out<<"enforce_windows_groups=true\n";}
    denied=false;try{read_policy();}catch(...){denied=true;}assert(denied);
    const auto encrypted=root/L"protected.dat";
    const std::string clear="CONFIDENTIAL_POINT_TEST_SECRET_ABCDEF1234567890";
    point::compliance::write_user_protected_file(encrypted,clear);
    assert(point::compliance::read_user_protected_file(encrypted)==clear);
    std::ifstream input(encrypted,std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    assert(bytes.find(clear)==std::string::npos);input.close();
    std::filesystem::remove_all(root);
    std::cout<<"PASS: actual Windows policy parsing and DPAPI encrypted file round trip.\n";
}
