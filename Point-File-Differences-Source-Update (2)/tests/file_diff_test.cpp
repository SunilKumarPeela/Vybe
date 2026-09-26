#include "point_file_diff.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace point;
using namespace point::file_diff;
DataSet data(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows) {
    DataSet d;d.headers=std::move(headers);d.rows=std::move(rows);return d;
}
int main() {
    auto a=data({"Employee ID","Location"},{{"001","Greeley"},{"002","Denver"},{"003","Austin"}});
    auto b=data({"EID","Office"},{{"001","Greeley"},{"002","Seattle"},{"004","Phoenix"}});
    auto r=compare(a,b,{{0,0},{1,1}});
    assert(r.left_only==2 && r.right_only==2 && r.matched_left==1);
    assert(r.rows[0].left && r.rows[0].index==1 && !r.rows[2].left && r.rows[2].index==1);
    r=compare(a,b,{{0,0}});assert(r.left_only==1 && r.right_only==1); // Ignore unselected changes.
    b.rows={{"003","Austin"},{"001","Greeley"},{"002","Denver"}};
    r=compare(a,b,{{0,0},{1,1}});assert(r.rows.empty()); // Order independent.
    a=data({"id"},{{"x"},{"x"},{"x"}}); b=data({"id"},{{"x"}});
    r=compare(a,b,{{0,0}});assert(r.left_only==2 && r.right_only==0 && r.rows[0].index==1);
    Options options;options.count_duplicates=false;
    r=compare(a,b,{{0,0}},options);assert(r.rows.empty() && r.matched_left==3 && r.matched_right==1);
    r=compare(b,a,{{0,0}},options);assert(r.rows.empty() && r.matched_right==3);
    a=data({"id"},{{" "},{""},{"001"},{"A-B"},{"Mary Ann"},{"FOO"}});
    b=data({"id"},{{""},{" "},{"1"},{"AB"},{"MaryAnn"},{"foo"}});
    r=compare(a,b,{{0,0}});assert(r.left_only==6 && r.right_only==6);
    options={};options.match_empty_keys=true;options.ignore_ascii_case=true;
    r=compare(a,b,{{0,0}},options);assert(r.left_only==3 && r.right_only==3);
    a=data({"a","b"},{{"x:y","z"},{"", "a:b"},{"x"}});
    b=data({"c","d"},{{"x","y:z"},{"a",":b"},{"x",""}});
    r=compare(a,b,{{0,0},{1,1}});assert(r.left_only==2 && r.right_only==2); // No composite collisions; ragged rows.
    a=data({"a"},{{" x "}});b=data({"b"},{{"x"}});
    assert(compare(a,b,{{0,0}}).rows.empty());
    options={};options.trim_whitespace=false;assert(compare(a,b,{{0,0}},options).rows.size()==2);
    a=data({"a"},{});b=data({"b"},{{"x"}});assert(compare(a,b,{{0,0}}).right_only==1);
    b.rows.clear();assert(compare(a,b,{{0,0}}).rows.empty());
    int invalid=0;
    for(const auto& fields:std::vector<std::vector<FieldPair>>{{},{{1,0}},{{0,0},{0,0}}}) {
        try{compare(a,b,fields);}catch(const std::invalid_argument&){++invalid;}
    } assert(invalid==3);
    a.rows={{"x"}};bool cancelled=false;
    try{compare(a,b,{{0,0}},{},[]{return true;});}catch(const std::runtime_error&){cancelled=true;}
    assert(cancelled);
    // 100,000 rows each; 1,000 changed values and 1,000 extra B occurrences.
    a=data({"id","value"},{});b=data({"key","value"},{});
    for(int i=0;i<100000;++i){
        a.rows.push_back({std::to_string(i),"same"});
        b.rows.push_back({std::to_string(i),i<1000?"changed":"same"});
    }
    for(int i=0;i<1000;++i)b.rows.push_back({std::to_string(2000+i),"same"});
    const auto begin=std::chrono::steady_clock::now();
    r=compare(a,b,{{0,0},{1,1}});
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    assert(r.left_only==1000 && r.right_only==2000 && r.matched_left==99000 && r.matched_right==99000);
    std::cout<<"PASS: field pairing, both sides, ordering, duplicate counts/presence, blanks, leading zeros, punctuation, case, whitespace, ragged rows, empty files, invalid mapping, cancellation.\n";
    std::cout<<"PASS: 100000 A / 101000 B rows; 1000 only A, 2000 only B, 99000 matches; "<<ms<<" ms (comparison only).\n";
}
