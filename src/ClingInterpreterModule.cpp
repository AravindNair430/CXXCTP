﻿#include "ClingInterpreterModule.h"

/// \note module loading order is important
std::map<std::string, std::vector<std::string>> InterpreterModule::moduleToSources {
    /// \note module with app loop must be loaded last
    {
        "main_module",
        std::vector<std::string>{
#if defined(CLING_IS_ON)
            "../resources/ctp_scripts/app_loop.cpp"
#endif // CLING_IS_ON
        }
    }
};

bool InterpreterModule::isClingReady = false;

std::shared_ptr<DispatchQueue> InterpreterModule::receivedMessagesQueue_{};

std::map<std::string, std::unique_ptr<InterpreterModule>> InterpreterModule::interpMap{};

std::mutex InterpreterModule::clingReadyMutex{};

std::condition_variable InterpreterModule::clingReadyCV{};

void processCode(cling::Interpreter& interp, const std::string& code) {
    cling::Value res; // Will hold the result of the expression evaluation.
    interp.process(code.c_str(), &res);
}

void executeCode(cling::Interpreter& interp, const std::string& code) {
    interp.execute(code.c_str());
}

void removeClingModule(const std::string& module_id) {
    if(InterpreterModule::interpMap.find(module_id) != InterpreterModule::interpMap.end()) {
        printf("erased module %s\n", module_id.c_str());
        InterpreterModule::interpMap.erase(module_id);
    } else {
        printf("module not found! %s\n", module_id.c_str());
        for(const auto& it : InterpreterModule::interpMap) {
            printf("maybe you wanted to type %s?\n", it.first.c_str());
        }
    }
}

InterpreterModule::InterpreterModule(const string &id, const std::vector<string> &moduleFiles)
    : id_(id)
    , moduleFiles_(moduleFiles)
    //, moduleThread(&InterpreterModule::threadMain, this)
{
    for(const auto& it: moduleFiles_) {
        printf("moduleFiles_ module %s\n", it.c_str());
    }
    //moduleThread.detach();
    createInterpreter();
}

InterpreterModule::~InterpreterModule() {
    if(!moduleFiles_.empty()) {
        cling::Interpreter::CompilationResult compilationResult;
        metaProcessor_->process(id_ + "_cling_shutdown();", compilationResult, nullptr, true);
    }
}

void add_default_cling_args(std::vector<string> &args) {
    args.push_back("EmbedCling");
    args.push_back("-I.");
    args.push_back("-I../");

    //args.push_back("-nobuiltininc");
    //args.push_back("-nostdinc");

    //args.push_back("std=c++17");
    //args.push_back("-std=c++17");
    args.push_back("--std=c++17");
    //args.push_back("--std=c++2a");
    //args.push_back("-fmodules-ts");

    args.push_back("-I../cling-build/build/lib/clang/5.0.0/include");
    args.push_back("-I../cling-build/src/include/");
    args.push_back("-I../cling-build/build/include/");
    args.push_back("-I../cling-build/src/tools/clang/include/");
    args.push_back("-I../cling-build/build/tools/clang/include/");
    args.push_back("-I../cling-build/src/tools/cling/include/");

    //args.push_back("-I../submodules/Jinja2Cpp/thirdparty/nonstd/expected-light/include/");

    args.push_back("-I../resources");

    // https://stackoverflow.com/a/30877725
    args.push_back("-DBOOST_SYSTEM_NO_DEPRECATED");
    args.push_back("-DBOOST_ERROR_CODE_HEADER_ONLY");
    // https://jinja2cpp.dev/docs/build_and_install.html#dependency-management-modes
    //args.push_back("-Dvariant_CONFIG_SELECT_VARIANT=variant_VARIANT_NONSTD");

    //args.push_back("-I/usr/local/tander/i586-pc-linux-gnu/usr/lib/gcc/i586-pc-linux-gnu/7.3.0/include/g++-v7/");
    //args.push_back("-I/usr/local/tander/i586-pc-linux-gnu/usr/lib/gcc/i586-pc-linux-gnu/7.3.0/include/g++-v7/i586-pc-linux-gnu/");
    //args.push_back("-I/usr/local/tander/i586-pc-linux-gnu/usr/include/");

    //args.push_back("-I/usr/include/c++/8/");
    //args.push_back("-I/usr/include/c++/7/");

    //args.push_back("-I/usr/include/x86_64-linux-gnu/");
}

void InterpreterModule::createInterpreter() {
    std::vector<std::string> args;
    add_default_cling_args(args);

    std::vector< const char* > interp_args;
    {
        std::vector< std::string >::const_iterator iarg;
        for( iarg = args.begin() ; iarg != args.end() ; ++iarg ) {
            interp_args.push_back(iarg->c_str());
        }
    }
    interpreter_ = std::make_unique<cling::Interpreter>(
                interp_args.size(), &(interp_args[0]), LLVMDIR/*, {}, false*/);
    interpreter_->AddIncludePath(".");
    interpreter_->AddIncludePath("../");
    //interpreter_->process("#include <iostream>\n");
    //interpreter_->process("#include <string>\n");
    //interpreter_->process("#include <optional>\n");
    interpreter_->enableDynamicLookup(true);
    metaProcessor_ = std::make_unique<cling::MetaProcessor>(*interpreter_, llvm::outs());

    interpreter_->process("#define CLING_IS_ON 1");

    interpreter_->declare("int aGlobal;\n"
                          "float getAnotherGlobal();\n"
                          "void setAnotherGlobal(float val);\n");

    /*interpreter_->process("#include <string>");
        interpreter_->process("#include <map>");
        interpreter_->process("#include <memory>");

        interpreter_->declare("std::map<std::string, std::unique_ptr<InterpreterModule>> interpMap;\n");*/

    printf("created module %s\n", id_.c_str());
}

void InterpreterModule::prepare() {
    cling::Interpreter::CompilationResult compilationResult;

    if(!moduleFiles_.empty()) {
        for(const auto& it: moduleFiles_) {
            printf("processes module %s\n", it.c_str());
            metaProcessor_->process(".x " + it, compilationResult, nullptr, true);
        }

        metaProcessor_->process(id_ + "_cling_prepare();", compilationResult, nullptr);
    }
}

void InterpreterModule::run() {
    if(!moduleFiles_.empty()) {
        std::thread module_main([this](){
            cling::Interpreter::CompilationResult compilationResult;
            metaProcessor_->process(id_ + "_cling_run();", compilationResult, nullptr, true);
        });
        module_main.detach();
    }
}

void reloadClingModule(const string &module_id, const std::vector<string> &sources) {
    InterpreterModule::interpMap[module_id] = std::make_unique<InterpreterModule>(
                module_id.c_str(),
                sources);
    InterpreterModule::interpMap[module_id]->prepare();
    InterpreterModule::interpMap[module_id]->run();
}

void reloadAllCling() {
    /// \note remove all old modules and create new modules
    for(const auto& it : InterpreterModule::interpMap) {
        printf("erased module %s\n", it.first.c_str());
        InterpreterModule::interpMap.erase(it.first);
    }

    llvm::outs() << "LLVMDIR is " << LLVMDIR << '\n';

    for(const auto& it : InterpreterModule::moduleToSources) {
        reloadClingModule(it.first, it.second);
        llvm::outs() << "reloaded module " << it.first << '\n';
    }

    cling::Value res; // Will hold the result of the expression evaluation.
}

[[ noreturn ]] void cling_func() {
    InterpreterModule::receivedMessagesQueue_ =
            std::make_shared<DispatchQueue>(std::string{"Cling Dispatch Queue"}, 0);

    InterpreterModule::receivedMessagesQueue_->dispatch([] {
        llvm::outs() << "dispatch reloadAllCling 1!... " << '\n';
        reloadAllCling();
        {
            std::unique_lock<std::mutex> lk(InterpreterModule::clingReadyMutex);
            InterpreterModule::isClingReady = true;
        }
        InterpreterModule::clingReadyCV.notify_one();
    });

    while(true) {
        InterpreterModule::receivedMessagesQueue_->DispatchQueued();
        if(InterpreterModule::receivedMessagesQueue_->isEmpty()) {
            /*for(const auto& it : interpMap) {
                interpMap[it.first]->run_update();
            }*/
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
