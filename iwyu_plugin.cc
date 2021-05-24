#include <map>                          // for map, swap, etc
#include <memory>                       // for unique_ptr
#include <set>                          // for set, set<>::iterator, swap
#include <string>                       // for string, operator+, etc
#include <unordered_map>
#include <utility>                      // for pair, make_pair

// This is needed for
// preprocessor_info().PublicHeaderIntendsToProvide().  Somehow IWYU
// removes it mistakenly.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Preprocessor.h"


#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"

#include "iwyu_ast_util.h"
#include "iwyu_globals.h"
#include "iwyu_lexer_utils.h"
#include "iwyu_location_util.h"
#include "iwyu_verrs.h"

using llvm::errs;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Token;
using clang::StringRef;
using clang::FileEntry;
using clang::Module;

using include_what_you_use::GetFileEntry;
using include_what_you_use::GetLocation;

using std::unordered_map;
using std::set;

struct FileContext {
public:
  unordered_map<const FileEntry*, set<SourceLocation>> includes_;
  unordered_map<const Module*, set<SourceLocation>> modules_;
  unordered_map<const FileEntry*, set<SourceLocation>> used_imports_;
  set<FileContext*> associations_;
  const FileEntry* file_;
  const SourceManager& sm_;
public:
  FileContext(const FileEntry* file, const SourceManager& sm) : file_(file), sm_(sm) {}
  void import(const FileEntry* file, SourceLocation loc) {
    includes_[file].insert(loc);
  }
  void import(const Module* file, SourceLocation loc) {
    modules_[file].insert(loc);
  }
  void associate(FileContext* file) {
    associations_.insert(file);
  }
  void full_use(const clang::NamedDecl* decl, SourceLocation loc) {
    VERRS(6) << "full_use: " << decl->getQualifiedNameAsString() << "\n";
  }
  void fwd_use(const clang::NamedDecl* decl, SourceLocation loc) {
    VERRS(6) << "fwd_use: " << decl->getName() << "\n";
  }
  void import_use(const clang::FileEntry* file, SourceLocation include_loc) {
    VERRS(6) << "Full use import: " << file->getName() << " @ " << include_loc.printToString(sm_) << "\n";
    used_imports_[file].insert(include_what_you_use::GetLineBeginning(include_loc, sm_));
  }
  void import_use(const clang::FileID fid) {
    import_use(sm_.getFileEntryForID(fid), sm_.getIncludeLoc(fid));
  }

  bool Processed = false;

  bool hasAssociated(const FileEntry* file) const {
    if (file_ == file) return true;
    for (FileContext* child : associations_)
      if (child->hasAssociated(file))
        return true;
    return false;
  }

  bool imports(const FileEntry* entry) const {
    return used_imports_.find(entry) != used_imports_.cend();
  }

  bool imports_transitively(const FileEntry* entry) const {
    for (const FileContext* assoc : associations_)
      if (imports(assoc->file_) && (assoc->imports(entry) || assoc->imports_transitively(entry))) return true;
    return false;
  }

};

void PrintImportPath(SourceLocation loc, const SourceManager& sm) {
  VERRS(6) << "Import path:\n";
  clang::PresumedLoc ploc = sm.getPresumedLoc(loc);
  while (ploc.getFileID() != sm.getMainFileID()) {
    loc = ploc.getIncludeLoc();
    ploc = sm.getPresumedLoc(loc);
    VERRS(6) << "  > " << loc.printToString(sm) << "\n";
  }
}

SourceLocation GetIncludeLocInMainFile(const SourceManager& sm, SourceLocation loc) {
  clang::PresumedLoc ploc = sm.getPresumedLoc(loc);
  loc = SourceLocation();
  while (ploc.getFileID() != sm.getMainFileID()) {
    loc = ploc.getIncludeLoc();
    ploc = sm.getPresumedLoc(loc);
  }
  return loc;
}

class MyContext {
 public:
  MyContext(const SourceManager& sm) : sm_(sm) {
    const FileEntry* main = getMainFileEntry();
    files_.insert({main, {main, sm_}});
  }

  const FileEntry* getMainFileEntry() const {
    return sm_.getFileEntryForID(sm_.getMainFileID());
  }

  FileContext& getMainFileContext() {
    return files_.find(getMainFileEntry())->second;
  }

  const FileContext& getMainFileContext() const {
    return files_.find(getMainFileEntry())->second;
  }

  bool isAssociated(SourceLocation loc) const {
    return isAssociated(GetFileEntry(loc, sm_));
  }

  FileContext& file(const FileEntry* file) {
    auto it = files_.find(file);
    if (it == files_.cend()) {
      it = files_.emplace(std::make_pair(file, FileContext(file, sm_))).first;
    }
    return it->second;
  }

  FileContext& file(SourceLocation loc) {
    return file(GetFileEntry(loc, sm_));
  }

  bool isAssociated(const FileEntry* entry) const {
    return getMainFileContext().hasAssociated(entry);
  }

  void import(SourceLocation HashLoc, const FileEntry* File) {
    file(GetFileEntry(HashLoc, sm_)).import(File, HashLoc);
  }

  void import(SourceLocation HashLoc, const Module* Module) {
    file(GetFileEntry(HashLoc, sm_)).import(Module, HashLoc);
  }

  void full_use(const clang::NamedDecl* decl, SourceLocation loc) {
    file(loc).full_use(decl, loc);
    auto ploc = sm_.getPresumedLoc(GetLocation(decl));

    VERRS(6) 
      << "import use " << ploc.getFileID().getHashValue() 
      << " @ " << ploc.getIncludeLoc().printToString(sm_)
      << " fe: " << sm_.getFileEntryForID(ploc.getFileID())
      << "\n";
    VERRS(6)
      << "imported in " << GetIncludeLocInMainFile(sm_, GetLocation(decl)).printToString(sm_) << "\n";
    PrintImportPath(GetLocation(decl), sm_);
    file(loc).import_use(ploc.getFileID());

    /* auto fid = sm_.getFileID(GetLocation(decl)); */
    /* auto file = GetFileEntry(decl, sm_); */
    /* VERRS(6) << "full use of file id: " << fid.getHashValue() << "\n"; */
    /* VERRS(6) << "file: " << (file ? file->getName() : "<null>") << "\n"; */
    /* VERRS(6) << "loc: " << GetLocation(decl).printToString(sm_) << "\n"; */
    /* VERRS(6) << "loc: " << decl->getLocation().printToString(sm_) << "\n"; */
    /* VERRS(6) << "ismacro: " << (GetLocation(decl).isMacroID() ? "1" : "0") << "\n"; */
    /* VERRS(6) << "incld: " << sm_.getIncludeLoc(fid).printToString(sm_) << "\n"; */
    /* VERRS(6) << ploc.getIncludeLoc().printToString(sm_) << "\n"; */
    /* VERRS(6) << ploc.getFileID().getHashValue() << "\n"; */
    /* VERRS(6) << sm_.getFileEntryForID(ploc.getFileID()) << "\n"; */
    /* VERRS(6) << ploc.getFilename() << "\n"; */
    /* bool invalid = false; */
    /* auto e = sm_.getSLocEntry(fid, &invalid); */
    /* VERRS(6) << "invalid: " << invalid << "\n"; */
    /* if (!invalid) */
    /*   VERRS(6) << "e: file: " << e.isFile() << ", exp: " << e.isExpansion() << "\n"; */
  }
  void fwd_use(const clang::NamedDecl* decl, SourceLocation loc) {
    file(loc).fwd_use(decl, loc);
  }
  unordered_map<const FileEntry*, FileContext> files_ = {};
  const SourceManager& sm_;
 private:
};

class MyPP : public clang::PPCallbacks {
public:
  MyPP(MyContext& m) : context(m) {}
protected:
 void InclusionDirective(SourceLocation HashLoc, const Token& IncludeTok,
                         StringRef FileName, bool IsAngled,
                         clang::CharSourceRange FilenameRange,
                         const FileEntry* File, StringRef SearchPath,
                         StringRef RelativePath, const Module* Imported,
                         clang::SrcMgr::CharacteristicKind FileType) override {

   if (Imported)
     context.import(HashLoc, Imported);
   else
     context.import(HashLoc, File);
 }

private:
 MyContext& context;
};

static bool Belongs(const FileEntry* f1, const FileEntry* f2) {
  using include_what_you_use::GetCanonicalName;
  using include_what_you_use::GetFilePath;
  std::string n1 = GetCanonicalName(GetFilePath(f1));
  std::string n2 = GetCanonicalName(GetFilePath(f2));

  return std::equal(
      std::begin(n1), std::find(std::begin(n1), std::end(n1), '+'),
      std::begin(n2), std::find(std::begin(n2), std::end(n2), '+'));
}

static void SetAssociatedFiles(MyContext& context, const FileEntry* file) {
  auto& file_context = context.file(file);
  for (auto it : file_context.includes_) {
    const FileEntry* includee = it.first;
    if (Belongs(file, includee)) {
      file_context.associate(&context.file(includee));
      SetAssociatedFiles(context, it.first);
    }
  }
}

void Process(FileContext& file, clang::ASTContext& context) {
  for (auto& assoc : file.associations_) {
    Process(*assoc, context);
  }

  if (file.Processed) return;

  VERRS(6) << "Processing " << file.file_->getName() << "\n";
  auto associations = file.associations_;
  for (FileContext* assoc : associations) {
    VERRS(6) << "assoc: " << assoc->file_->getName();
    if (file.used_imports_.find(assoc->file_) == file.used_imports_.cend()) {
      file.associations_.erase(assoc);
      VERRS(6) << " [unused]";
    }
    VERRS(6) << "\n";
  }

  clang::SourceManager& sm = context.getSourceManager();
  for (const auto& it : file.includes_) {
    VERRS(6) << " " << it.first << ": " << it.first->getName() << "\n";

    set<SourceLocation> used_locs = {};
    auto it_used_imports = file.used_imports_.find(it.first);
    if (it_used_imports != file.used_imports_.cend()) {
      used_locs = it_used_imports->second;
    }

    for (SourceLocation loc : it.second) {
      if (used_locs.find(loc) == used_locs.cend()) {
        auto& DE = context.getDiagnostics();
        const auto diag_id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, "'%0' is unused");
        clang::SourceRange range = include_what_you_use::GetLineRange(loc, sm);
        auto DB = DE.Report(loc, diag_id);
        auto name = include_what_you_use::Basename(it.first->getName().str());
        DB.AddString(name);
        DB.AddFixItHint(clang::FixItHint::CreateRemoval(clang::CharSourceRange::getCharRange(range)));
      }
    }
  }

  for (const auto& it : file.used_imports_) {
    if (file.includes_.find(it.first) != file.includes_.cend()) {
      continue;
    }
    VERRS(6) << "used import: " << it.first->getName() << "\n";
    for (auto loc : it.second) {
      VERRS(6) << " " << loc.printToString(sm) << "\n";
    }
    if (!file.imports_transitively(it.first)) {
      SourceLocation loc = *it.second.begin();
      if (loc.isInvalid()) continue;

      clang::SourceRange lineRange = include_what_you_use::GetLineRange(loc, sm);
      loc = include_what_you_use::GetLineBeginning(GetIncludeLocInMainFile(sm, loc), sm);

      auto& DE = context.getDiagnostics();
      const auto diag_id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, "'%0' is missing");
      auto DB = DE.Report(loc, diag_id);
      auto name = include_what_you_use::Basename(it.first->getName().str());
      DB.AddString(name);
      DB.AddFixItHint(clang::FixItHint::CreateInsertionFromRange(loc, clang::CharSourceRange(lineRange, true)));
    }
  }
  file.Processed = true;
}

#include "clang/AST/RecursiveASTVisitor.h"
class MyASTConsumer : public clang::ASTConsumer, public clang::RecursiveASTVisitor<MyASTConsumer> {
public:
  MyASTConsumer(std::unique_ptr<MyContext> context): context_(std::move(context)) {}

  bool ShouldVisit(clang::Decl *decl) {
    return context_->isAssociated(GetLocation(decl));
  }
  bool ShouldVisit(clang::Expr *expr) {
    return context_->isAssociated(GetLocation(expr));
  }

  void FullUse(clang::NamedDecl* decl, SourceLocation loc) {
    context_->full_use(decl, loc);
  }

  void FwdUse(clang::NamedDecl* decl, SourceLocation loc) {
    context_->fwd_use(decl, loc);
  }

  bool VisitObjCContainerDecl(clang::ObjCContainerDecl* decl) {
    if (!ShouldVisit(decl)) return true;

    if (include_what_you_use::IsForwardDecl(decl)) {
      SourceLocation loc = GetLocation(decl);
      FwdUse(decl, loc);
    }

    return true;
  }

  bool VisitObjCInterfaceDecl(clang::ObjCInterfaceDecl* decl) {
    if (!ShouldVisit(decl)) return true;

    if (decl->isThisDeclarationADefinition()) {
      SourceLocation loc = GetLocation(decl);
      if (decl->getSuperClass())
        FullUse(decl->getSuperClass(), loc);

      for (const auto d : decl->getReferencedProtocols()) {
        FullUse(d, loc);
      }
    }

    return true;
  }

  /* bool VisitObjCObjectPointerType(clang::ObjCObjectPointerType* type) { */
  /*   if (CanIgnoreCurrentASTNode()) return true; */
  /*   const bool full_use = !type->getTypeArgs().empty(); */

  /*   for (const auto* qual : type->quals()) { */
  /*     ReportDeclForwardDeclareUse(CurrentLoc(), qual); */
  /*   } */

  /*   // id<...> does not have an interface */
  /*   if (const ObjCInterfaceDecl* iface = type->getInterfaceDecl()) { */
  /*     if (full_use) { */
  /*       FullUse(iface, loc); */
  /*     } else { */
  /*       ReportDeclForwardDeclareUse(CurrentLoc(), iface); */
  /*     } */
  /*   } */

  /*   return true; */
  /* } */

  bool VisitObjCImplDecl(clang::ObjCImplDecl* decl) {
    if (!ShouldVisit(decl)) return true;
    FullUse(decl->getClassInterface(), GetLocation(decl));
    return true;
  }

  /* bool VisitObjCProtocolDecl(clang::ObjCProtocolDecl* decl) { */
  /*   if (!ShouldVisit(decl)) return true; */

  /*   if (decl->isThisDeclarationADefinition()) */
  /*     for (const auto* d : decl->getReferencedProtocols()) */
  /*       FullUse(d, GetLocation(decl)); */

  /*   return true; */
  /* } */

  bool VisitObjCMessageExpr(clang::ObjCMessageExpr* expr) {
    if (!ShouldVisit(expr)) return true;
    SourceLocation loc = GetLocation(expr);
    FullUse(expr->getReceiverInterface(), loc);
    FullUse(expr->getMethodDecl(), loc);
    return true;
  }


protected:
  void HandleTranslationUnit(clang::ASTContext& context) override {
    clang::SourceManager& sm = context.getSourceManager();
    const FileEntry* main_file = sm.getFileEntryForID(sm.getMainFileID());
    SetAssociatedFiles(*context_, main_file);
    TraverseDecl(context.getTranslationUnitDecl());
    Process(context_->file(main_file), context);
  }


private:
  std::unique_ptr<MyContext> context_;
};

class MyAction: public clang::PluginASTAction {
 public:
   std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI,
       clang::StringRef InFile) override
   {
     auto context = std::make_unique<MyContext>(CI.getSourceManager());
     CI.getPreprocessor().addPPCallbacks(std::make_unique<MyPP>(*context));

     return std::make_unique<MyASTConsumer>(std::move(context));
   }

   bool ParseArgs(const clang::CompilerInstance &CI,
       const std::vector<std::string> &arg) override
   {
     return true;
   }
};

#define PLUGIN

#ifdef PLUGIN
static clang::FrontendPluginRegistry::Add<MyAction>
Plugin("IWYU", "Include What You Use");
#else
llvm::cl::OptionCategory MyToolCategory("mytool options");

int main(int argc, const char *argv[]) {
  using namespace clang::tooling;

  auto factory = newFrontendActionFactory<MyAction>();
  
  auto optionsParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!optionsParser) {
    errs() << optionsParser.takeError();
    return 1;
  }
  ClangTool Tool(optionsParser->getCompilations(),
                 optionsParser->getSourcePathList());

  return Tool.run(factory.get());
}
#endif
