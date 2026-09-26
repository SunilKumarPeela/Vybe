#include "point_security_rules.h"
#include "point_file_validation.h"
#include "point_core.h"
#include <stdexcept>
#include <exception>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(    std::string("Check failed at line ") + std::to_string(__LINE__) + ": " #expression); } while (false)
int main(int argc,char** argv) {
    const char* stage = "policy and filename validation";
    try {
    std::cout << "RUN: security regression tests" << std::endl;
    using namespace point::security_rules;
    CHECK(boolean("TRUE") && !boolean("false"));
    for(const auto* value:{"tru","", "falsehood", "yes please"}) {
        bool rejected=false;try{boolean(value);}catch(...){rejected=true;}CHECK(rejected);
    }
    for(const auto* value:{"0","3651","-1","30days","3.5","", "999999999999999999"}) {
        bool rejected=false;try{retention_days(value);}catch(...){rejected=true;}CHECK(rejected);
    }
    CHECK(retention_days("30")==30 && retention_days("3650")==3650);
    for(const auto* name:{L"../report.csv",L"C:\\report.csv",L"file.csv:payload",L"CON.csv",L"NUL.xlsx",L"COM1.xls",L"report.xlsm",L"bad\n.csv"})
        CHECK(!safe_report_name(name));
    CHECK(safe_report_name(L"AD Users.xlsx"));CHECK(safe_report_name(L"Users.CSV"));
    CHECK(!point_validation::looks_like_html("Event,Status\nsign in,access denied\n"));
    CHECK(point_validation::looks_like_html(" \r\n<!DOCTYPE HTML><html>login</html>"));
    CHECK(!point_validation::looks_like_html("Name,Comment\nAlex,<script is text here\n"));
    stage = "CSV validation";
    const auto root=std::filesystem::temp_directory_path() / ("point-security-regression-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);point::ensure_directories(root);
    {std::ofstream f(root/"valid.csv");f<<"Event,Status\nsign in,access denied\n";}
    std::wstring reason;CHECK(point_validation::validate(root/"valid.csv",L"valid.csv",reason));
    CHECK(!point_validation::validate(root/"valid.csv",L"valid.xlsm",reason));
    stage = "audit creation and concurrent append";
    point::append_audit(root,"TEST","one\nforged\tentry");
    std::vector<std::thread> threads;
    std::array<std::exception_ptr,4> errors{};
    for(std::size_t i=0;i<errors.size();++i)threads.emplace_back([root,&errors,i]{
        try { for(int j=0;j<100;++j)point::append_audit(root,"TEST","concurrent"); }
        catch (...) { errors[i]=std::current_exception(); }
    });
    for(auto& thread:threads)thread.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    stage = "audit-chain verification";
    std::filesystem::path log;
    for(const auto& f:std::filesystem::directory_iterator(root/"Logs"))if(f.path().extension()==".log")log=f.path();
    CHECK(!log.empty());std::ifstream input(log);std::string line,previous(64,'0');int count=0;
    while(std::getline(input,line)) {
        auto end=line.find_last_of('\t'),start=line.find_last_of('\t',end-1);
        CHECK(line.substr(start+1,end-start-1)==previous);previous=line.substr(end+1);++count;
    }CHECK(count==401);
    input.close(); // Windows must release the log before alteration and directory cleanup.
    stage = "truncated audit rejection";
    {std::ofstream f(log,std::ios::app);f<<"partial";}
    bool rejected=false;try{point::append_audit(root,"TEST","must fail");}catch(...){rejected=true;}CHECK(rejected);
    if(argc>1) {
        stage = "ZIP fixtures";
        const auto fixtures=std::filesystem::path(argv[1]);
        for(const auto* name:{"clean.xlsx","renamed-macro.xlsx","oversized.xlsx"})
            if(!std::filesystem::is_regular_file(fixtures/name))
                throw std::runtime_error("Missing test fixture: " + (fixtures/name).string());
        CHECK(point_validation::validate(fixtures/"clean.xlsx",L"clean.xlsx",reason));
        CHECK(!point_validation::validate(fixtures/"renamed-macro.xlsx",L"renamed-macro.xlsx",reason));
        CHECK(!point_validation::validate(fixtures/"oversized.xlsx",L"oversized.xlsx",reason));
    }
    stage = "test-directory cleanup";
    std::filesystem::remove_all(root);
    std::cout<<"PASS: strict policy values, safe filenames, macro-format rejection, legitimate security-report CSV, HTML rejection, serialized daily audit chain, incomplete-log failure.\n";
    return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: security_regression_test [" << stage << "]: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FAIL: security_regression_test [" << stage << "]: unknown exception" << std::endl;
        return 1;
    }
}
