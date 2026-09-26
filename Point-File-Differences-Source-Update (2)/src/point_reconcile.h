#pragma once
#include "point_core.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace point::reconcile {
constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
enum class Role { Key, Value };
struct Pair { std::size_t left = absent, right = absent; Role role = Role::Value; };
struct Options { bool trim_outer = true, ignore_ascii_case = false, all_fields = false; };
enum class Kind { Modified, OnlyA, OnlyB, Ambiguous, MissingKey, FieldAdded, FieldRemoved };
struct Row {
    Kind kind = Kind::Modified;
    std::size_t left = absent, right = absent;
    // Indexes into Report.fields. Schema changes are shared, not repeated per record.
    std::vector<std::size_t> changed;
    std::size_t schema_field = absent, count_a = 0, count_b = 0;
};
struct Report {
    std::vector<Pair> keys, fields;
    std::vector<std::size_t> schema_fields;
    std::vector<Row> rows;
    std::size_t unchanged = 0, modified = 0, only_a = 0, only_b = 0;
    std::size_t ambiguous_groups = 0, ambiguous_rows = 0, missing_keys = 0;
};
using Aliases = std::unordered_map<std::string,std::string>;
inline Aliases aliases(const std::vector<FieldSynonymGroup>& user = {}) {
    Aliases output;
    auto add = [&](const std::string& canonical, std::initializer_list<const char*> names) {
        output[canonical] = canonical;
        for (auto name : names) output[name] = canonical;
    };
    add("employeeid", {"emplid","employid","eid","empid","employeenumber","employeeno","empnumber"});
    add("firstname", {"firstnm","firsnm","fname","givenname"});
    add("lastname", {"lastnm","lname","surname","familyname"});
    add("username", {"samaccountname","samaccount","loginname","logonname"});
    add("email", {"emailaddress","mail"});
    add("department", {"dept","departmentname"});
    add("location", {"office","officelocation"});
    for (const auto& group : user) {
        const auto raw = normalize_name(group.canonical_field);
        const auto canonical = output.contains(raw) ? output.at(raw) : raw;
        output[raw] = canonical;
        for (const auto& name : group.synonyms) output[normalize_name(name)] = canonical;
    }
    return output;
}
inline std::string canonical(const std::string& name, const Aliases& names) {
    auto normalized = normalize_name(name);
    const auto found = names.find(normalized);
    return found == names.end() ? normalized : found->second;
}
inline std::string value(const DataSet& data, std::size_t row, std::size_t col) {
    if (row == absent || col == absent || row >= data.rows.size() || col >= data.rows[row].size()) return {};
    return data.rows[row][col];
}
inline std::string normalized(std::string text, const Options& options) {
    if (options.trim_outer) text = trim(text);
    if (options.ignore_ascii_case)
        for (char& ch : text) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}
inline std::pair<std::string,bool> record_key(const DataSet& data, std::size_t row,
        const std::vector<Pair>& keys, bool left, const Options& options) {
    std::string key;
    bool missing = false;
    for (const auto& pair : keys) {
        auto text = normalized(value(data,row,left ? pair.left : pair.right),options);
        missing = missing || text.empty(); // Every component of an identity key is required.
        key += std::to_string(text.size()) + ':' + text;
    }
    return {std::move(key),missing};
}
// Suggest only one-to-one header matches; unknown abbreviations require explicit mapping.
inline std::vector<Pair> suggest(const std::vector<std::string>& a,
        const std::vector<std::string>& b, const Aliases& names) {
    std::unordered_map<std::string,std::vector<std::size_t>> left, right;
    for (std::size_t i=0;i<a.size();++i) left[canonical(a[i],names)].push_back(i);
    for (std::size_t i=0;i<b.size();++i) right[canonical(b[i],names)].push_back(i);
    std::vector<Pair> result;
    for (std::size_t i=0;i<a.size();++i) {
        const auto key=canonical(a[i],names);
        const auto found=right.find(key);
        if(left.at(key).size()==1 && found!=right.end() && found->second.size()==1)
            result.push_back({i,found->second.front(),Role::Value});
    }
    // Prefer an employee identifier, then username, then email. Names are never guessed as IDs.
    for (const auto* preferred : {"employeeid","username","email"}) {
        auto found=std::find_if(result.begin(),result.end(),[&](const Pair& pair) {
            return canonical(a[pair.left],names)==preferred;
        });
        if(found!=result.end()) {found->role=Role::Key;break;}
    }
    return result;
}
inline Report prepare(const DataSet& a,const DataSet& b,const std::vector<Pair>& requested,
        const Options& options,const Aliases& names) {
    Report output;
    std::unordered_set<std::size_t> used_a,used_b;
    for(const auto& pair:requested) {
        if(pair.left>=a.headers.size() || pair.right>=b.headers.size())
            throw std::invalid_argument("Every selected pair needs a field on each side.");
        if(!used_a.insert(pair.left).second || !used_b.insert(pair.right).second)
            throw std::invalid_argument("A field is mapped more than once. Remove its old pair first.");
        (pair.role==Role::Key ? output.keys : output.fields).push_back(pair);
    }
    if(output.keys.empty()) throw std::invalid_argument("Add a record-key pair, such as EmplID -> EID.");
    if(options.all_fields) {
        std::unordered_map<std::string,std::vector<std::size_t>> left,right;
        for(std::size_t i=0;i<a.headers.size();++i) if(!used_a.contains(i)) left[canonical(a.headers[i],names)].push_back(i);
        for(std::size_t i=0;i<b.headers.size();++i) if(!used_b.contains(i)) right[canonical(b.headers[i],names)].push_back(i);
        for(std::size_t i=0;i<a.headers.size();++i) {
            if(used_a.contains(i)) continue;
            const auto key=canonical(a.headers[i],names);
            const auto found=right.find(key);
            if(found==right.end()) output.fields.push_back({i,absent,Role::Value});
            else {
                if(left.at(key).size()!=1 || found->second.size()!=1)
                    throw std::invalid_argument("Ambiguous header mapping for '"+a.headers[i]+"'. Pair these columns manually before comparing all fields.");
                const auto j=found->second.front();
                output.fields.push_back({i,j,Role::Value});used_b.insert(j);
            }
        }
        for(std::size_t j=0;j<b.headers.size();++j)
            if(!used_b.contains(j)) output.fields.push_back({absent,j,Role::Value});
    }
    for(std::size_t i=0;i<output.fields.size();++i) {
        const auto& field=output.fields[i];
        if(field.left==absent || field.right==absent) {
            output.schema_fields.push_back(i);
            Row row;row.kind=field.left==absent?Kind::FieldAdded:Kind::FieldRemoved;
            row.schema_field=i;output.rows.push_back(std::move(row));
        }
    }
    return output;
}
inline Report compare(const DataSet& a,const DataSet& b,const std::vector<Pair>& requested,
        const Options& options={},const Aliases& names=aliases(),
        const std::function<bool()>& cancelled={}) {
    auto check=[&] {if(cancelled && cancelled()) throw std::runtime_error("Comparison cancelled.");};
    check();
    auto result=prepare(a,b,requested,options,names);
    struct Group {std::size_t a=absent,b=absent,na=0,nb=0;};
    std::unordered_map<std::string,Group> groups;
    std::vector<std::size_t> next_a(a.rows.size(),absent), next_b(b.rows.size(),absent);
    std::vector<bool> missing_a(a.rows.size(),false),missing_b(b.rows.size(),false);
    auto index=[&](const DataSet& data,bool left,std::vector<std::size_t>& next,std::vector<bool>& missing) {
        for(std::size_t end=data.rows.size();end>0;--end) {
            check();const auto i=end-1;
            auto [key,empty]=record_key(data,i,result.keys,left,options);
            if(empty) {missing[i]=true;continue;}
            auto& group=groups[std::move(key)];
            if(left) {next[i]=group.a;group.a=i;++group.na;}
            else {next[i]=group.b;group.b=i;++group.nb;}
        }
    };
    index(a,true,next_a,missing_a);index(b,false,next_b,missing_b);
    auto emit=[&](Kind kind,std::size_t left,std::size_t right,const Group* group=nullptr) {
        Row row;row.kind=kind;row.left=left;row.right=right;
        if(group) {row.count_a=group->na;row.count_b=group->nb;}
        result.rows.push_back(std::move(row));
    };
    auto ambiguous=[&](const Group& group) {
        ++result.ambiguous_groups;
        for(auto i=group.a;i!=absent;i=next_a[i]) {check();emit(Kind::Ambiguous,i,absent,&group);++result.ambiguous_rows;}
        for(auto i=group.b;i!=absent;i=next_b[i]) {check();emit(Kind::Ambiguous,absent,i,&group);++result.ambiguous_rows;}
    };
    for(std::size_t i=0;i<a.rows.size();++i) {
        check();
        if(missing_a[i]) {emit(Kind::MissingKey,i,absent);++result.missing_keys;continue;}
        const auto key=record_key(a,i,result.keys,true,options).first;
        const auto& group=groups.at(key);
        if(group.a!=i) continue;
        if(group.na>1 || group.nb>1) {ambiguous(group);continue;}
        if(group.nb==0) {emit(Kind::OnlyA,i,absent);++result.only_a;continue;}
        Row row;row.kind=Kind::Modified;row.left=i;row.right=group.b;
        for(std::size_t f=0;f<result.fields.size();++f) {
            check();const auto& field=result.fields[f];
            if(field.left==absent || field.right==absent) continue;
            if(normalized(value(a,i,field.left),options)!=normalized(value(b,group.b,field.right),options))
                row.changed.push_back(f);
        }
        if(!row.changed.empty() || !result.schema_fields.empty()) {++result.modified;result.rows.push_back(std::move(row));}
        else ++result.unchanged;
    }
    for(std::size_t j=0;j<b.rows.size();++j) {
        check();
        if(missing_b[j]) {emit(Kind::MissingKey,absent,j);++result.missing_keys;continue;}
        const auto& group=groups.at(record_key(b,j,result.keys,false,options).first);
        if(group.na!=0 || group.b!=j) continue;
        if(group.nb>1) ambiguous(group);
        else {emit(Kind::OnlyB,absent,j);++result.only_b;}
    }
    return result;
}
inline bool is_schema(const Row& row) {return row.kind==Kind::FieldAdded || row.kind==Kind::FieldRemoved;}
inline bool changed(const Report& report,const Row& row,std::size_t field) {
    if(is_schema(row)) return row.schema_field==field;
    if(row.kind==Kind::OnlyA || row.kind==Kind::OnlyB) return true;
    if(row.kind!=Kind::Modified) return false;
    const auto& mapping=report.fields.at(field);
    return mapping.left==absent || mapping.right==absent ||
        std::binary_search(row.changed.begin(),row.changed.end(),field);
}
inline std::string label(Kind kind,bool before_after) {
    switch(kind) {
    case Kind::Modified:return before_after?"Modified":"Value mismatch";
    case Kind::OnlyA:return before_after?"Removed":"Only in database (A)";
    case Kind::OnlyB:return before_after?"Added":"Only in AD (B)";
    case Kind::Ambiguous:return "Duplicate key - review";
    case Kind::MissingKey:return "Missing key - review";
    case Kind::FieldAdded:return before_after?"Field added":"Field only in B";
    case Kind::FieldRemoved:return before_after?"Field removed":"Field only in A";
    }
    return {};
}
inline std::string field_label(const DataSet& a,const DataSet& b,const Pair& field) {
    return (field.left==absent?"(absent)":a.headers[field.left])+" -> "+
        (field.right==absent?"(absent)":b.headers[field.right]);
}
inline std::string protected_value(const std::string& header,const std::string& text) {
    if(looks_like_payment_card_number(trim(text)))
        throw std::runtime_error("Export blocked: possible payment card number detected.");
    return is_highly_sensitive_field(header)?mask_sensitive_value(text):text;
}
inline std::string key_text(const DataSet& a,const DataSet& b,const Report& report,const Row& row,bool protect=false) {
    if(is_schema(row)) return "(schema)";
    const bool left=row.left!=absent;const auto& data=left?a:b;const auto index=left?row.left:row.right;
    std::string text;
    for(const auto& pair:report.keys) {
        const auto col=left?pair.left:pair.right;
        if(!text.empty()) text+="; ";
        text+=data.headers[col]+"="+(protect?protected_value(data.headers[col],value(data,index,col)):value(data,index,col));
    }
    return text;
}
inline std::string explanation(const Row& row) {
    if(row.kind==Kind::Ambiguous) return "Key occurrences A="+std::to_string(row.count_a)+", B="+
        std::to_string(row.count_b)+"; no automatic pairing.";
    if(row.kind==Kind::MissingKey) return "At least one record-key component is blank; no automatic pairing.";
    if(is_schema(row)) return "Column exists on only one side.";
    return {};
}
inline void csv_cell(std::ostream& out,const std::string& raw) {
    const auto text=escape_csv_for_spreadsheet(raw);
    out.put('"');for(char ch:text) {if(ch=='"') out.put('"');out.put(ch);}out.put('"');
}
// Long-form streaming export: one line per changed field, without constructing a huge table.
// Non-modified evidence rows include every original field on their own side.
inline void write_csv(std::ostream& out,const DataSet& a,const DataSet& b,const Report& report,
        const std::vector<std::size_t>& selected,bool before_after,const std::function<bool()>& cancelled={}) {
    auto check=[&] {if(cancelled && cancelled()) throw std::runtime_error("Export cancelled.");};
    auto write=[&](const std::vector<std::string>& cells) {
        check();for(std::size_t i=0;i<cells.size();++i) {if(i) out.put(',');csv_cell(out,cells[i]);}out<<"\r\n";
        if(!out) throw std::runtime_error("Difference export write failed.");
    };
    write({"Status","Key","A / Before Source","A / Before Parsed Row","B / After Source","B / After Parsed Row",
        "A / Before Field","B / After Field","A / Before Value","B / After Value","Detail"});
    for(const auto index:selected) {
        check();const auto& row=report.rows.at(index);
        const std::vector<std::string> base={label(row.kind,before_after),key_text(a,b,report,row,true),
            a.name,row.left==absent?"":std::to_string(row.left+2),b.name,row.right==absent?"":std::to_string(row.right+2)};
        auto emit=[&](std::size_t ac,std::size_t bc,bool schema=false) {
            auto cells=base;
            cells.push_back(ac==absent?"":a.headers[ac]);cells.push_back(bc==absent?"":b.headers[bc]);
            cells.push_back(schema?(ac==absent?"(column absent)":"(column present)"):
                (ac==absent?"":protected_value(a.headers[ac],value(a,row.left,ac))));
            cells.push_back(schema?(bc==absent?"(column absent)":"(column present)"):
                (bc==absent?"":protected_value(b.headers[bc],value(b,row.right,bc))));
            cells.push_back(explanation(row));write(cells);
        };
        if(is_schema(row)) {const auto& pair=report.fields.at(row.schema_field);emit(pair.left,pair.right,true);}
        else if(row.kind==Kind::Modified) {
            for(std::size_t f=0;f<report.fields.size();++f) if(changed(report,row,f))
                emit(report.fields[f].left,report.fields[f].right);
        } else if(row.left!=absent) {
            for(std::size_t col=0;col<a.headers.size();++col) emit(col,absent);
        } else {
            for(std::size_t col=0;col<b.headers.size();++col) emit(absent,col);
        }
    }
}
} // namespace point::reconcile
