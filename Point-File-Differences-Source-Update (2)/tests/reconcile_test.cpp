#include "point_reconcile.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <sstream>
using namespace point;
using namespace point::reconcile;
DataSet data(std::vector<std::string> heads,std::vector<std::vector<std::string>> rows) {
    DataSet d;d.headers=std::move(heads);d.rows=std::move(rows);return d;
}
int main() {
    auto names=aliases();
    auto a=data({"EmplID","FirstName","Location"},{{"001","Sunil","Greeley"},{"002","Alex","Denver"},{"003","Kim","Austin"}});
    auto b=data({"EID","Firsnm","Office"},{{"002","Alec","Denver"},{"001","Sunil","Greeley"},{"004","Dana","Phoenix"}});
    a.name="Database.csv";b.name="AD.csv";
    auto pairs=suggest(a.headers,b.headers,names);assert(pairs.size()==3 && pairs[0].role==Role::Key);
    auto r=compare(a,b,pairs);assert(r.unchanged==1 && r.modified==1 && r.only_a==1 && r.only_b==1);
    assert(r.rows[0].left==1 && r.rows[0].right==0 && r.rows[0].changed.size()==1);
    assert(changed(r,r.rows[0],0) && !changed(r,r.rows[0],1));
    assert(value(a,r.rows[0].left,r.fields[0].left)=="Alex");
    std::vector<std::size_t> selected(r.rows.size());std::iota(selected.begin(),selected.end(),0);
    std::ostringstream csv;write_csv(csv,a,b,r,selected,false);assert(csv.str().find("Alec")!=std::string::npos);
    // Composite keys and duplicate safety (all source rows retained, no guessed partner).
    a=data({"ID","Domain","Name"},{{"1","X","a"},{"1","Y","b"},{"1","X","c"},{"","Y","n"}});
    b=data({"Key","Domain","First"},{{"1","X","a"},{"1","Y","B"},{"2","X","z"},{"2","X","z"}});
    pairs={{0,0,Role::Key},{1,1,Role::Key},{2,2,Role::Value}};
    r=compare(a,b,pairs);assert(r.modified==1 && r.ambiguous_groups==2 && r.ambiguous_rows==5 && r.missing_keys==1);
    assert(r.only_a==0 && r.only_b==0 && r.rows.size()==7);
    // Partial missing key cannot match. Duplicate IDs on either side are review rows even when values equal.
    a.rows={{"1","","a"}};b.rows={{"1","","a"}};r=compare(a,b,pairs);assert(r.missing_keys==2 && r.unchanged==0);
    // Selected comparisons ignore unselected changes; all fields finds value and schema changes.
    a=data({"ID","FirstName","OldOnly","Department"},{{"001","Ann","","IT"}});
    b=data({"Key","Firsnm","NewOnly","Department"},{{"001","Ann","","HR"}});
    pairs={{0,0,Role::Key},{1,1,Role::Value}};
    r=compare(a,b,pairs);assert(r.unchanged==1 && r.rows.empty());
    Options full;full.all_fields=true;r=compare(a,b,pairs,full);assert(r.modified==1 && r.schema_fields.size()==2 && r.rows.size()==3);
    assert(r.rows.back().changed.size()==1); // Only ordinary cell changes stored per record.
    a.rows.clear();b.rows.clear();r=compare(a,b,pairs,full);assert(r.rows.size()==2 && r.schema_fields.size()==2);
    // Ambiguous synonym headers cannot be guessed by full comparison.
    a=data({"ID","FirstName","Firsnm"},{});b=data({"Key","GivenName"},{});
    bool invalid=false;try{compare(a,b,{{0,0,Role::Key}},full);}catch(const std::invalid_argument&){invalid=true;}assert(invalid);
    // Unknown abbreviations work when manually paired; values preserve zeros/punctuation.
    a=data({"EmplID","Firsnm"},{{"001","A-B"},{"2"," Mary Ann "},{"3","ANN"}});
    b=data({"EmployID","Whatever"},{{"1","AB"},{"2","Mary Ann"},{"3","ann"}});
    pairs={{0,0,Role::Key},{1,1,Role::Value}};r=compare(a,b,pairs);assert(r.only_a==1 && r.only_b==1 && r.modified==1 && r.unchanged==1);
    Options fold;fold.ignore_ascii_case=true;r=compare(a,b,pairs,fold);assert(r.modified==0 && r.unchanged==2);
    Options exact;exact.trim_outer=false;r=compare(a,b,pairs,exact);assert(r.modified==2);
    // Provenance-aware export masking and formula escaping for old/new values.
    a=data({"ID","Password","Name"},{{"1","oldsecret","safe"}});
    b=data({"Key","Password","Name"},{{"1","newsecret","=1+1"}});
    pairs={{0,0,Role::Key},{1,1,Role::Value},{2,2,Role::Value}};r=compare(a,b,pairs);
    csv.str("");write_csv(csv,a,b,r,{0},true);
    assert(csv.str().find("oldsecret")==std::string::npos && csv.str().find("newsecret")==std::string::npos);
    assert(csv.str().find("'=1+1")!=std::string::npos);
    b.rows[0][2]="4111111111111111";r=compare(a,b,pairs);bool blocked=false;
    try{write_csv(csv,a,b,r,{0},true);}catch(const std::runtime_error&){blocked=true;}assert(blocked);
    bool cancelled=false;try{compare(a,b,pairs,{},names,[]{return true;});}catch(const std::runtime_error&){cancelled=true;}assert(cancelled);
    invalid=false;try{compare(a,b,{{1,1,Role::Value}});}catch(const std::invalid_argument&){invalid=true;}assert(invalid);
    invalid=false;try{compare(a,b,{{0,0,Role::Key},{0,1,Role::Value}});}catch(const std::invalid_argument&){invalid=true;}assert(invalid);
    // Deterministic large-data correctness: above the original core's 100,000 result cap.
    a=data({"EmplID","FirstName","Department"},{});b=data({"EID","Firsnm","Dept"},{});
    for(int i=0;i<200000;++i){a.rows.push_back({std::to_string(i),"Name","IT"});b.rows.push_back({std::to_string(i),i<150000?"Changed":"Name","IT"});}
    b.rows.erase(b.rows.begin()+199999);b.rows.push_back({"new","Added","HR"});
    pairs=suggest(a.headers,b.headers,names);
    const auto begin=std::chrono::steady_clock::now();r=compare(a,b,pairs);
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    assert(r.modified==150000 && r.unchanged==49999 && r.only_a==1 && r.only_b==1 && r.rows.size()==150002);
    std::cout<<"PASS: synonym mappings, stable keys, mismatches, missing records, duplicates, composite keys, schemas, selected/all fields, normalization, masking, formulas, PAN block, cancellation.\n";
    std::cout<<"PASS: 200000 rows per file, 150000 modified + 1 removed + 1 added, no truncation; "<<ms<<" ms comparison only.\n";
}
