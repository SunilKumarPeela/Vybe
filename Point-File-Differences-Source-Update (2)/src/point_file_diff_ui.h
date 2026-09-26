#pragma once
#include "point_reconcile.h"
#include "point_compliance.h"
#include <atomic>
#include <climits>
#include <fstream>
#include <memory>
#include <thread>

// Included after Windows and common-control headers by point_win32.cpp.
namespace point::file_diff_ui {
constexpr UINT tick=1;
constexpr int file_a=301,file_b=302,field_a=303,field_b=304,pairs_a=305,pairs_b=306;
constexpr int add_pair=307,remove_pair=308,run=309,export_rows=310,cancel_run=311,results=312;
constexpr int trim_values=313,ignore_case=314,all_fields=315,add_key=316,filter_rows=317;
constexpr int mode=318,suggest_pairs=319,filter_field=320;
inline std::wstring wide(const std::string& text) {
    if(text.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring output(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),output.data(),count);
    return output;
}
struct Source {std::filesystem::path path;std::string name;std::vector<std::string> fields;};
struct State {
    HWND window=nullptr,group_a=nullptr,group_b=nullptr,group_result=nullptr,note=nullptr,status=nullptr;
    std::function<const Engine*()> get_engine;
    std::filesystem::path root;
    std::vector<Source> sources;
    reconcile::Aliases aliases;
    std::vector<reconcile::Pair> pairs;
    DataSet a,b;
    reconcile::Report report;
    std::vector<std::size_t> visible;
    std::thread worker;
    std::atomic<bool> done{false},cancelled{false};
    std::string error;
    bool before_after=false,busy=false,exporting=false,valid=false,closing=false;
    ~State() {cancelled.store(true);if(worker.joinable()) worker.join();}
};
inline HWND item(State& s,int id) {return GetDlgItem(s.window,id);}
inline int selection(HWND control) {return static_cast<int>(SendMessageW(control,CB_GETCURSEL,0,0));}
inline void message(State& s,const std::wstring& text) {SetWindowTextW(s.status,text.c_str());}
inline HWND control(State& s,const wchar_t* type,const wchar_t* text,DWORD style,int id) {
    HWND child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,100,24,s.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!child) throw std::runtime_error("Unable to create reconciliation control.");
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    return child;
}
inline void layout(State& s) {
    RECT area{};GetClientRect(s.window,&area);
    const int width=static_cast<int>(area.right),height=static_cast<int>(area.bottom),half=(width-36)/2;
    MoveWindow(item(s,mode),12,8,320,220,TRUE);
    MoveWindow(s.group_a,12,44,half,252,TRUE);MoveWindow(s.group_b,24+half,44,half,252,TRUE);
    for(int side=0;side<2;++side) {
        const int x=side==0?24:half+36;
        MoveWindow(item(s,side==0?file_a:file_b),x,70,half-24,300,TRUE);
        MoveWindow(item(s,side==0?field_a:field_b),x,110,half-24,300,TRUE);
        MoveWindow(item(s,side==0?pairs_a:pairs_b),x,146,half-24,136,TRUE);
    }
    MoveWindow(item(s,add_key),12,304,145,28,TRUE);
    MoveWindow(item(s,add_pair),165,304,155,28,TRUE);
    MoveWindow(item(s,remove_pair),328,304,155,28,TRUE);
    MoveWindow(item(s,suggest_pairs),491,304,165,28,TRUE);
    MoveWindow(item(s,run),664,304,145,28,TRUE);
    MoveWindow(item(s,cancel_run),817,304,90,28,TRUE);
    MoveWindow(item(s,trim_values),12,340,190,24,TRUE);
    MoveWindow(item(s,ignore_case),208,340,180,24,TRUE);
    MoveWindow(item(s,all_fields),394,340,width-410,24,TRUE);
    MoveWindow(s.note,16,371,width-32,40,TRUE);
    MoveWindow(s.group_result,12,418,width-24,std::max(135,height-470),TRUE);
    MoveWindow(item(s,filter_rows),205,425,220,220,TRUE);
    MoveWindow(item(s,filter_field),433,425,260,300,TRUE);
    MoveWindow(item(s,export_rows),701,423,200,28,TRUE);
    MoveWindow(item(s,results),24,461,width-48,std::max(65,height-525),TRUE);
    MoveWindow(s.status,16,height-43,width-32,36,TRUE);
}
inline void invalidate(State& s) {
    s.valid=false;s.visible.clear();ListView_SetItemCountEx(item(s,results),0,0);
    EnableWindow(item(s,export_rows),FALSE);
    message(s,L"Confirm the [KEY] mapping and compared fields, then click Compare Files.");
}
inline void list_pairs(State& s) {
    for(int id:{pairs_a,pairs_b}) {SendMessageW(item(s,id),LB_RESETCONTENT,0,0);SendMessageW(item(s,id),LB_SETHORIZONTALEXTENT,1000,0);}
    const int ai=selection(item(s,file_a)),bi=selection(item(s,file_b));if(ai<0 || bi<0) return;
    for(std::size_t i=0;i<s.pairs.size();++i) {
        const auto& pair=s.pairs[i];
        const auto prefix=std::to_wstring(i+1)+(pair.role==reconcile::Role::Key?L". [KEY] ":L". [COMPARE] ");
        const auto a=prefix+wide(s.sources[static_cast<std::size_t>(ai)].fields[pair.left]);
        const auto b=prefix+wide(s.sources[static_cast<std::size_t>(bi)].fields[pair.right]);
        SendMessageW(item(s,pairs_a),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(a.c_str()));
        SendMessageW(item(s,pairs_b),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(b.c_str()));
    }
}
inline void suggest(State& s) {
    s.pairs.clear();const int a=selection(item(s,file_a)),b=selection(item(s,file_b));
    if(a>=0 && b>=0) s.pairs=reconcile::suggest(s.sources[static_cast<std::size_t>(a)].fields,
        s.sources[static_cast<std::size_t>(b)].fields,s.aliases);
    list_pairs(s);invalidate(s);
}
inline void fields_changed(State& s,bool left) {
    const int index=selection(item(s,left?file_a:file_b));HWND fields=item(s,left?field_a:field_b);
    SendMessageW(fields,CB_RESETCONTENT,0,0);
    if(index>=0) {
        for(const auto& name:s.sources[static_cast<std::size_t>(index)].fields) {
            const auto text=wide(name);SendMessageW(fields,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(fields,CB_SETCURSEL,0,0);
    }
    suggest(s);
}
inline void mode_labels(State& s) {
    SetWindowTextW(s.window,s.before_after?L"Point - Compare Files: Before / After":L"Point - Reconcile Database vs AD");
    SetWindowTextW(s.group_a,s.before_after?L"1. BEFORE file / worksheet":L"1. DATABASE file / worksheet (A)");
    SetWindowTextW(s.group_b,s.before_after?L"2. AFTER file / worksheet":L"2. AD file / worksheet (B)");
    SetWindowTextW(s.note,L"[KEY] identifies the same record (for example EmplID -> EID). [COMPARE] checks its values (FirstName -> Firsnm).\r\nConfirm suggested pairs. Compare all fields also includes unselected columns; duplicate or incomplete keys are flagged, never guessed.");
    CheckDlgButton(s.window,all_fields,s.before_after?BST_CHECKED:BST_UNCHECKED);
    invalidate(s);
}
inline void columns(State& s) {
    HWND list=item(s,results);ListView_SetItemCountEx(list,0,0);while(ListView_DeleteColumn(list,0)) {}
    std::vector<std::wstring> names{L"Difference",L"Record key",L"A / Before row",L"B / After row",L"Changed fields / reason"};
    for(const auto& field:s.report.fields) {
        names.push_back(L"A / Before: "+(field.left==reconcile::absent?L"(absent)":wide(s.a.headers[field.left])));
        names.push_back(L"B / After: "+(field.right==reconcile::absent?L"(absent)":wide(s.b.headers[field.right])));
    }
    for(std::size_t i=0;i<names.size();++i) {
        LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=names[i].data();
        col.cx=i==1 || i==4?250:(i==2 || i==3?105:185);
        ListView_InsertColumn(list,static_cast<int>(i),&col);
    }
    SendMessageW(item(s,filter_field),CB_RESETCONTENT,0,0);
    SendMessageW(item(s,filter_field),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All changed fields"));
    for(const auto& field:s.report.fields) {
        const auto name=wide(reconcile::field_label(s.a,s.b,field));
        SendMessageW(item(s,filter_field),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(item(s,filter_field),CB_SETCURSEL,0,0);
}
inline void show_results(State& s) {
    s.visible.clear();const int filter=selection(item(s,filter_rows)),field=selection(item(s,filter_field));
    using reconcile::Kind;
    for(std::size_t i=0;i<s.report.rows.size();++i) {
        const auto& row=s.report.rows[i];
        if((filter==1 && row.kind!=Kind::Modified) || (filter==2 && row.kind!=Kind::OnlyA) ||
            (filter==3 && row.kind!=Kind::OnlyB) ||
            (filter==4 && row.kind!=Kind::Ambiguous && row.kind!=Kind::MissingKey) ||
            (filter==5 && !reconcile::is_schema(row))) continue;
        if(field>0 && !reconcile::changed(s.report,row,static_cast<std::size_t>(field-1))) continue;
        s.visible.push_back(i);
    }
    if(s.visible.size()>static_cast<std::size_t>(INT_MAX)) throw std::runtime_error("Too many rows for the Windows result control.");
    ListView_SetItemCountEx(item(s,results),static_cast<int>(s.visible.size()),LVSICF_NOSCROLL);
    InvalidateRect(item(s,results),nullptr,TRUE);
    message(s,L"Unchanged: "+std::to_wstring(s.report.unchanged)+L" | Changed: "+std::to_wstring(s.report.modified)+
        L" | Only A / Removed: "+std::to_wstring(s.report.only_a)+L" | Only B / Added: "+std::to_wstring(s.report.only_b)+
        L"\r\nDuplicate-key rows: "+std::to_wstring(s.report.ambiguous_rows)+L" | Missing keys: "+std::to_wstring(s.report.missing_keys)+
        L" | Schema changes: "+std::to_wstring(s.report.schema_fields.size())+L" | Showing: "+std::to_wstring(s.visible.size())+
        L". Red cells changed; double-click for all fields.");
}
inline void busy_controls(State& s,bool busy) {
    for(int id:{file_a,file_b,field_a,field_b,pairs_a,pairs_b,add_pair,add_key,remove_pair,run,trim_values,
            ignore_case,all_fields,filter_rows,filter_field,mode,suggest_pairs}) EnableWindow(item(s,id),!busy);
    EnableWindow(item(s,cancel_run),busy);EnableWindow(item(s,export_rows),!busy && s.valid);
}
inline void start(State& s) {
    if(s.busy) return;invalidate(s);
    const int ai=selection(item(s,file_a)),bi=selection(item(s,file_b));
    if(ai<0 || bi<0 || ai==bi) throw std::runtime_error("Select two different imported files or worksheets.");
    const auto* current=s.get_engine();if(!current) throw std::runtime_error("Import and refresh reports first.");
    auto copy_source=[&](int index) {
        const auto& source=s.sources[static_cast<std::size_t>(index)];
        for(const auto& ds:current->datasets()) if(ds.path==source.path && ds.name==source.name && ds.headers==source.fields) return ds;
        throw std::runtime_error("Imported sources changed. Close and reopen this mode.");
    };
    message(s,L"Preparing the two selected reports...");UpdateWindow(s.status);
    s.a=copy_source(ai);s.b=copy_source(bi);
    reconcile::Options options;
    options.trim_outer=IsDlgButtonChecked(s.window,trim_values)==BST_CHECKED;
    options.ignore_ascii_case=IsDlgButtonChecked(s.window,ignore_case)==BST_CHECKED;
    options.all_fields=IsDlgButtonChecked(s.window,all_fields)==BST_CHECKED;
    // Validate mappings before launching any asynchronous work.
    static_cast<void>(reconcile::prepare(s.a,s.b,s.pairs,options,s.aliases));
    s.report={};s.error.clear();s.cancelled.store(false);s.done.store(false);s.busy=true;s.exporting=false;
    busy_controls(s,true);message(s,L"Matching record keys and comparing fields... Cancel is available.");
    try {
        s.worker=std::thread([&s,options] {
            try {s.report=reconcile::compare(s.a,s.b,s.pairs,options,s.aliases,[&s] {return s.cancelled.load();});}
            catch(const std::exception& ex) {s.error=ex.what();}
            catch(...) {s.error="Comparison failed unexpectedly.";}
            s.done.store(true);
        });
    } catch(...) {s.busy=false;busy_controls(s,false);throw;}
}
inline void export_result(State& s) {
    if(!s.valid || s.busy) return;
    const auto policy=compliance::load_policy(compliance::application_directory());compliance::authorize_current_user(policy);
    if(!compliance::current_user_can_export(policy)) throw std::runtime_error("Export requires Point Exporters membership.");
    s.error.clear();s.cancelled.store(false);s.done.store(false);s.busy=true;s.exporting=true;busy_controls(s,true);
    message(s,L"Exporting all changes for displayed records... Cancel is available.");
    try {
        s.worker=std::thread([&s] {
            const auto destination=s.root/L"Exports"/L"point-file-changes.csv";
            auto temporary=destination;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
            try {
                {
                    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                    if(!output) throw std::runtime_error("Unable to create difference export.");
                    reconcile::write_csv(output,s.a,s.b,s.report,s.visible,s.before_after,[&s] {return s.cancelled.load();});
                    output.close();if(!output) throw std::runtime_error("Could not finish writing the export.");
                }
                if(s.cancelled.load()) throw std::runtime_error("Export cancelled.");
                if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Could not publish export. Close the previous CSV if it is open.");
            } catch(const std::exception& ex) {std::error_code ignored;std::filesystem::remove(temporary,ignored);s.error=ex.what();}
            catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);s.error="Export failed unexpectedly.";}
            s.done.store(true);
        });
    } catch(...) {s.busy=false;busy_controls(s,false);throw;}
}
inline std::string summary(State& s,const reconcile::Row& row) {
    if(row.kind!=reconcile::Kind::Modified && !reconcile::is_schema(row)) return reconcile::explanation(row);
    std::string output;
    for(std::size_t f=0;f<s.report.fields.size();++f) if(reconcile::changed(s.report,row,f)) {
        if(!output.empty()) output+="; ";output+=reconcile::field_label(s.a,s.b,s.report.fields[f]);
        if(output.size()>512) {output+=" ... (double-click for all)";break;}
    }
    return output;
}
inline std::wstring table_text(State& s,const reconcile::Row& row,int col) {
    if(col==0) return wide(reconcile::label(row.kind,s.before_after));
    if(col==1) return wide(reconcile::key_text(s.a,s.b,s.report,row));
    if(col==2) return row.left==reconcile::absent?L"":std::to_wstring(row.left+2);
    if(col==3) return row.right==reconcile::absent?L"":std::to_wstring(row.right+2);
    if(col==4) return wide(summary(s,row));
    if(col<5) return {};
    const auto index=static_cast<std::size_t>((col-5)/2);if(index>=s.report.fields.size()) return {};
    const bool left=(col-5)%2==0;const auto& pair=s.report.fields[index];const auto field=left?pair.left:pair.right;
    if(reconcile::is_schema(row)) {
        if(index!=row.schema_field) return {};
        return field==reconcile::absent?L"(column absent)":L"(column present)";
    }
    if((left?row.left:row.right)==reconcile::absent) return L"(no record)";
    if(field==reconcile::absent) return L"(column absent)";
    return wide(reconcile::value(left?s.a:s.b,left?row.left:row.right,field));
}
inline LRESULT CALLBACK detail_edit_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR subclass,DWORD_PTR) {
    if(msg==WM_COPY || msg==WM_CUT || msg==WM_CONTEXTMENU) return 0;
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(window,detail_edit_proc,subclass);
    return DefSubclassProc(window,msg,wp,lp);
}
inline LRESULT CALLBACK detail_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {
        auto create=reinterpret_cast<CREATESTRUCTW*>(lp);auto text=static_cast<std::wstring*>(create->lpCreateParams);
        HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text->c_str(),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL,8,8,700,450,window,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(edit,detail_edit_proc,1,0);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return 0;
    }
    if(msg==WM_SIZE) {MoveWindow(GetDlgItem(window,1),8,8,std::max(1,static_cast<int>(LOWORD(lp))-16),
        std::max(1,static_cast<int>(HIWORD(lp))-16),TRUE);return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
inline std::string display_value(const std::string& header,const std::string& text) {
    return is_highly_sensitive_field(header) || looks_like_payment_card_number(trim(text))?mask_sensitive_value(text):text;
}
inline void details(State& s,int selected) {
    if(!s.valid || selected<0 || static_cast<std::size_t>(selected)>=s.visible.size()) return;
    const auto& row=s.report.rows[s.visible[static_cast<std::size_t>(selected)]];
    std::string text=reconcile::label(row.kind,s.before_after)+"\r\n"+reconcile::explanation(row)+"\r\n\r\n";
    if(row.kind==reconcile::Kind::Modified || reconcile::is_schema(row)) {
        text+="CHANGED FIELDS\r\n";
        for(std::size_t i=0;i<s.report.fields.size();++i) if(reconcile::changed(s.report,row,i)) {
            const auto& pair=s.report.fields[i];text+=reconcile::field_label(s.a,s.b,pair)+"\r\n";
            if(reconcile::is_schema(row)) text+="Column presence changed.\r\n\r\n";
            else {
                text+="  A / Before: "+(pair.left==reconcile::absent?"(column absent)":display_value(s.a.headers[pair.left],reconcile::value(s.a,row.left,pair.left)))+"\r\n";
                text+="  B / After:  "+(pair.right==reconcile::absent?"(column absent)":display_value(s.b.headers[pair.right],reconcile::value(s.b,row.right,pair.right)))+"\r\n\r\n";
            }
        }
    }
    auto append=[&](const DataSet& data,std::size_t index,const char* label) {
        if(index==reconcile::absent) return;
        text+=std::string(label)+": "+data.name+"; parsed row "+std::to_string(index+2)+"\r\n";
        for(std::size_t i=0;i<data.headers.size();++i)
            text+=data.headers[i]+": "+display_value(data.headers[i],reconcile::value(data,index,i))+"\r\n";
        text+="\r\n";
    };
    append(s.a,row.left,"A / BEFORE FULL RECORD");append(s.b,row.right,"B / AFTER FULL RECORD");
    auto content=wide(text);WNDCLASSW cls{};cls.lpfnWndProc=detail_proc;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"PointFileDifferenceDetail";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Record Changes - Before / After",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,850,650,s.window,nullptr,cls.hInstance,&content);if(window) ShowWindow(window,SW_SHOW);
}
inline LRESULT custom_draw(State& s,NMLVCUSTOMDRAW* draw) {
    if(draw->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
    if(draw->nmcd.dwDrawStage!=(CDDS_ITEMPREPAINT|CDDS_SUBITEM)) return CDRF_DODEFAULT;
    const auto index=static_cast<std::size_t>(draw->nmcd.dwItemSpec);if(index>=s.visible.size()) return CDRF_DODEFAULT;
    const auto& row=s.report.rows[s.visible[index]];const int col=draw->iSubItem;
    draw->clrText=RGB(30,35,40);draw->clrTextBk=index%2==0?RGB(255,255,255):RGB(246,248,250);
    if(row.kind==reconcile::Kind::Ambiguous || row.kind==reconcile::Kind::MissingKey) {
        draw->clrText=RGB(120,70,0);draw->clrTextBk=RGB(255,243,210);
    } else if(col==0 || (col>=5 && reconcile::changed(s.report,row,static_cast<std::size_t>((col-5)/2)))) {
        draw->clrText=RGB(170,0,0);draw->clrTextBk=RGB(255,225,225);
    }
    return CDRF_NEWFONT;
}
inline LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        state=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);state->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(!state) return DefWindowProcW(window,msg,wp,lp);auto& s=*state;
    try {
        switch(msg) {
        case WM_CREATE: {
            s.group_a=control(s,L"BUTTON",L"",BS_GROUPBOX,401);s.group_b=control(s,L"BUTTON",L"",BS_GROUPBOX,402);
            s.group_result=control(s,L"BUTTON",L"3. Differences / Changes",BS_GROUPBOX,403);
            for(int id:{file_a,file_b,field_a,field_b,filter_rows,filter_field,mode}) {
                control(s,L"COMBOBOX",L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,id);
                SendMessageW(item(s,id),CB_SETDROPPEDWIDTH,650,0);
            }
            for(int id:{pairs_a,pairs_b}) control(s,L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|WS_BORDER|LBS_NOTIFY,id);
            control(s,L"BUTTON",L"Add KEY Pair",WS_TABSTOP|BS_PUSHBUTTON,add_key);
            control(s,L"BUTTON",L"Add COMPARE Pair",WS_TABSTOP|BS_PUSHBUTTON,add_pair);
            control(s,L"BUTTON",L"Remove Selected Pair",WS_TABSTOP|BS_PUSHBUTTON,remove_pair);
            control(s,L"BUTTON",L"Suggest Field Pairs",WS_TABSTOP|BS_PUSHBUTTON,suggest_pairs);
            control(s,L"BUTTON",L"Compare Files",WS_TABSTOP|BS_DEFPUSHBUTTON,run);
            control(s,L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,cancel_run);
            control(s,L"BUTTON",L"Export Displayed Records",WS_TABSTOP|BS_PUSHBUTTON,export_rows);
            control(s,L"BUTTON",L"Trim outer whitespace",WS_TABSTOP|BS_AUTOCHECKBOX,trim_values);
            control(s,L"BUTTON",L"Ignore case (A-Z)",WS_TABSTOP|BS_AUTOCHECKBOX,ignore_case);
            control(s,L"BUTTON",L"Compare ALL fields (auto-map synonyms; include added / removed columns)",WS_TABSTOP|BS_AUTOCHECKBOX,all_fields);
            s.note=control(s,L"STATIC",L"",0,404);s.status=control(s,L"STATIC",L"",0,405);
            control(s,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS,results);
            ListView_SetExtendedListViewStyle(item(s,results),LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
            const auto* engine=s.get_engine();
            if(engine) {s.aliases=reconcile::aliases(engine->field_synonyms());for(const auto& ds:engine->datasets()) s.sources.push_back({ds.path,ds.name,ds.headers});}
            for(const auto& ds:s.sources) {
                const auto text=wide(ds.name)+L"  ["+ds.path.wstring()+L"]";
                for(int id:{file_a,file_b}) SendMessageW(item(s,id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            }
            SendMessageW(item(s,file_a),CB_SETCURSEL,0,0);SendMessageW(item(s,file_b),CB_SETCURSEL,s.sources.size()>1?1:0,0);
            for(const auto* text:{L"Reconcile: Database vs AD",L"Compare: Before / After"})
                SendMessageW(item(s,mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
            SendMessageW(item(s,mode),CB_SETCURSEL,s.before_after?1:0,0);
            for(const auto* text:{L"All differences",L"Value changes",L"Only A / Removed",L"Only B / Added",L"Duplicate / Missing keys",L"Schema changes"})
                SendMessageW(item(s,filter_rows),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
            SendMessageW(item(s,filter_rows),CB_SETCURSEL,0,0);
            SendMessageW(item(s,filter_field),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All changed fields"));
            SendMessageW(item(s,filter_field),CB_SETCURSEL,0,0);
            CheckDlgButton(window,trim_values,BST_CHECKED);fields_changed(s,true);fields_changed(s,false);mode_labels(s);busy_controls(s,false);layout(s);
            if(!SetTimer(window,tick,100,nullptr)) throw std::runtime_error("Unable to create progress timer.");return 0;
        }
        case WM_SIZE:layout(s);return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={960,740};return 0;
        case WM_COMMAND: {
            const int id=LOWORD(wp),action=HIWORD(wp);if(s.busy && id!=cancel_run) return 0;
            if((id==file_a || id==file_b) && action==CBN_SELCHANGE) fields_changed(s,id==file_a);
            else if(id==mode && action==CBN_SELCHANGE) {s.before_after=selection(item(s,mode))==1;mode_labels(s);}
            else if((id==pairs_a || id==pairs_b) && action==LBN_SELCHANGE) {
                const auto selected=SendMessageW(item(s,id),LB_GETCURSEL,0,0);
                SendMessageW(item(s,id==pairs_a?pairs_b:pairs_a),LB_SETCURSEL,static_cast<WPARAM>(selected),0);
            } else if((id==add_pair || id==add_key) && action==BN_CLICKED) {
                const int a=selection(item(s,field_a)),b=selection(item(s,field_b));
                if(a<0 || b<0) throw std::runtime_error("Select a field in each block.");
                for(const auto& pair:s.pairs) if(pair.left==static_cast<std::size_t>(a) || pair.right==static_cast<std::size_t>(b))
                    throw std::runtime_error("This field is already paired. Remove its existing pair before changing its role.");
                s.pairs.push_back({static_cast<std::size_t>(a),static_cast<std::size_t>(b),id==add_key?reconcile::Role::Key:reconcile::Role::Value});
                list_pairs(s);invalidate(s);
            } else if(id==remove_pair && action==BN_CLICKED) {
                const auto selected=SendMessageW(item(s,pairs_a),LB_GETCURSEL,0,0);
                if(selected>=0 && static_cast<std::size_t>(selected)<s.pairs.size()) {s.pairs.erase(s.pairs.begin()+selected);list_pairs(s);invalidate(s);}
            } else if(id==suggest_pairs && action==BN_CLICKED) suggest(s);
            else if(id==run && action==BN_CLICKED) start(s);
            else if(id==cancel_run && action==BN_CLICKED) {s.cancelled.store(true);message(s,L"Cancelling...");}
            else if(id==export_rows && action==BN_CLICKED) export_result(s);
            else if((id==filter_rows || id==filter_field) && action==CBN_SELCHANGE && s.valid) show_results(s);
            else if((id==trim_values || id==ignore_case || id==all_fields) && action==BN_CLICKED) invalidate(s);
            return 0;
        }
        case WM_TIMER:
            if(s.busy && s.done.load()) {
                s.worker.join();s.busy=false;
                if(!s.exporting) s.valid=s.error.empty();busy_controls(s,false);
                if(s.closing) {DestroyWindow(window);return 0;}
                if(!s.error.empty()) message(s,wide(s.error));
                else if(s.exporting) {
                    message(s,L"Saved Exports\\point-file-changes.csv: every changed field for the displayed records, with A/Before and B/After values.");
                    append_audit(s.root,"FILE_CHANGES_EXPORT",std::to_string(s.visible.size())+" evidence rows");
                } else {
                    columns(s);show_results(s);
                    append_audit(s.root,"FILE_RECONCILIATION","modified="+std::to_string(s.report.modified)+";only_a="+
                        std::to_string(s.report.only_a)+";only_b="+std::to_string(s.report.only_b)+";review_rows="+
                        std::to_string(s.report.ambiguous_rows+s.report.missing_keys));
                }
            }
            return 0;
        case WM_NOTIFY: {
            auto hdr=reinterpret_cast<NMHDR*>(lp);
            if(hdr->idFrom==results && hdr->code==NM_CUSTOMDRAW) return custom_draw(s,reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            if(hdr->idFrom==results && hdr->code==LVN_GETDISPINFOW) {
                auto info=reinterpret_cast<NMLVDISPINFOW*>(lp);
                if(!(info->item.mask&LVIF_TEXT) || info->item.iItem<0 || static_cast<std::size_t>(info->item.iItem)>=s.visible.size()) return 0;
                const auto text=table_text(s,s.report.rows[s.visible[static_cast<std::size_t>(info->item.iItem)]],info->item.iSubItem);
                if(info->item.pszText && info->item.cchTextMax>0) lstrcpynW(info->item.pszText,text.c_str(),info->item.cchTextMax);
            } else if(hdr->idFrom==results && hdr->code==NM_DBLCLK) details(s,reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem);
            return 0;
        }
        case WM_CLOSE:
            if(s.busy) {s.closing=true;s.cancelled.store(true);message(s,L"Cancelling and closing...");}else DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,tick);return 0;
        case WM_NCDESTROY:s.window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }
    } catch(const std::exception& ex) {
        if(msg==WM_CREATE) {MessageBoxW(window,wide(ex.what()).c_str(),L"File Comparison",MB_ICONERROR);return -1;}
        MessageBoxW(window,wide(ex.what()).c_str(),L"File Comparison",MB_ICONWARNING);
    }
    return DefWindowProcW(window,msg,wp,lp);
}
inline void open(HWND owner,const std::function<const Engine*()>& get_engine,const std::filesystem::path& root,bool before_after=false) {
    const auto* engine=get_engine();
    if(!engine || engine->datasets().size()<2) {MessageBoxW(owner,L"Import and refresh at least two files or worksheets first.",L"File Comparison",MB_ICONINFORMATION);return;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"PointFileDifferences";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    cls.hIcon=reinterpret_cast<HICON>(SendMessageW(owner,WM_GETICON,ICON_BIG,0));
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return;
    State state;state.get_engine=get_engine;state.root=root;state.before_after=before_after;
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Point - File Comparison",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1200,840,owner,nullptr,cls.hInstance,&state);if(!window) return;
    EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
    MSG msg{};bool quit=false;int quit_code=0;
    while(state.window) {
        const BOOL got=GetMessageW(&msg,nullptr,0,0);if(got<=0) {quit=got==0;quit_code=static_cast<int>(msg.wParam);break;}
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && (msg.hwnd==window || IsChild(window,msg.hwnd))) {SendMessageW(window,WM_CLOSE,0,0);continue;}
        if(!IsDialogMessageW(window,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(state.window) DestroyWindow(state.window);
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(quit) PostQuitMessage(quit_code);
}
} // namespace point::file_diff_ui
