#pragma once
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
namespace point::security_rules {
inline bool boolean(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if(value=="true" || value=="yes" || value=="1") return true;
    if(value=="false" || value=="no" || value=="0") return false;
    throw std::runtime_error("Invalid security-policy boolean: use true or false.");
}
inline int retention_days(const std::string& value) {
    if(value.empty() || value.size()>4 || !std::all_of(value.begin(),value.end(),[](unsigned char ch){return ch>='0' && ch<='9';}))
        throw std::runtime_error("Retention must be an integer from 1 through 3650.");
    const int days=std::stoi(value);
    if(days<1 || days>3650) throw std::runtime_error("Retention must be from 1 through 3650 days.");
    return days;
}
}

namespace point::security_rules {
inline bool safe_report_name(const std::wstring& name) {
    if(name.empty() || name.size()>240 || name.find_first_of(L"/\\:*?\"<>|")!=std::wstring::npos ||
        name.back()==L'.' || name.back()==L' ') return false;
    for(wchar_t ch:name) if(ch<32 || ch==127) return false;
    auto lower=name;
    for(auto& ch:lower) if(ch>=L'A' && ch<=L'Z') ch=static_cast<wchar_t>(ch-L'A'+L'a');
    const auto dot=lower.find_last_of(L'.');if(dot==std::wstring::npos || dot==0) return false;
    const auto ext=lower.substr(dot);
    if(ext!=L".csv" && ext!=L".xlsx") return false;
    auto stem=lower.substr(0,lower.find(L'.'));
    while(!stem.empty() && (stem.back()==L' ' || stem.back()==L'.')) stem.pop_back();
    if(stem==L"con" || stem==L"prn" || stem==L"aux" || stem==L"nul") return false;
    if(stem.size()==4 && (stem.rfind(L"com",0)==0 || stem.rfind(L"lpt",0)==0) && stem[3]>=L'0' && stem[3]<=L'9') return false;
    return true;
}
}
