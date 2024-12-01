
#include "polyglat-c/pch.h"

#include <polyglat-shared/annotation.h>

class TypeInfoEmitter final : public clang::RecursiveASTVisitor<TypeInfoEmitter> {

    clang::Sema* Sema;

    static auto ShouldBeAnnotated(const clang::NamedDecl *decl) -> bool {
        auto target = false;
        for (const auto attr : decl->attrs()) {
            if (const auto annotationAttr = dyn_cast<clang::AnnotateAttr>(attr)) {
                target = annotationAttr->getAnnotation().starts_with(polyglat::shared::Namespace);
            } else if (const auto annotationTypeAttr = dyn_cast<clang::AnnotateTypeAttr>(attr)) {
                target = annotationTypeAttr->getAnnotation().starts_with(polyglat::shared::Namespace);
            }
            if (target) {
                break;
            }
        }
        return target;
    }

  public:
    explicit TypeInfoEmitter(clang::Sema* sema) : Sema(sema) {}

    // ReSharper disable once CppMemberFunctionMayBeStatic
    auto VisitRecordDecl(clang::RecordDecl *decl) -> bool {
        if (ShouldBeAnnotated(decl)) {
            polyglat::shared::CreateAnnotation(decl);
        }

        return true;
    }

    // ReSharper disable once CppMemberFunctionMayBeStatic
    // ReSharper disable once CppMemberFunctionMayBeConst
    auto VisitFunctionDecl(clang::FunctionDecl *decl) -> bool {
        if (!ShouldBeAnnotated(decl)) {
            return true;
        }

        /*
         * Parent record should be exported if its member is requested to be exported.
         * If parent record wasn't exported, Backend couldn't recover interface
         */
        if (const auto parent = llvm::dyn_cast<clang::TypeDecl>(decl->getParent());
            !decl->isStatic() && parent && !ShouldBeAnnotated(parent)) {

            auto &diag = Sema->getDiagnostics();
            const auto diagId = diag.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "Member function in not-exported record cannot be exported");
            diag.Report(decl->getLocation(), diagId);
            return false;
        }

        polyglat::shared::CreateAnnotation(decl);

        for (const auto arg : decl->parameters()) {
            polyglat::shared::CreateAnnotation(arg);
        }

        return true;
    }
};

class TypeInfoASTConsumer final : public clang::SemaConsumer {
    clang::Sema* Sema = nullptr;

  public:
    void HandleTranslationUnit(clang::ASTContext &Ctx) override {
        TypeInfoEmitter emitter{ Sema };
        emitter.TraverseDecl(Ctx.getTranslationUnitDecl());
    }

    void InitializeSema(clang::Sema &S) override {
        Sema = &S;
    }
};

class TypeInfoEmitAction final : public clang::PluginASTAction {
  public:
    auto CreateASTConsumer(clang::CompilerInstance & /**/, llvm::StringRef /**/) -> std::unique_ptr<clang::ASTConsumer> override {
        return std::make_unique<TypeInfoASTConsumer>();
    }

    auto ParseArgs(const clang::CompilerInstance & /**/, const std::vector<std::string> & /**/) -> bool override {
        return true;
    }

    auto getActionType() -> ActionType override {
        return AddBeforeMainAction;
    }
};

namespace {
    [[maybe_unused]]
    // NOLINTNEXTLINE(cert-err58-cpp) : Initialization of 'X' with static storage duration may throw an exception that cannot be caught
    const clang::FrontendPluginRegistry::Add<TypeInfoEmitAction> X("polyglat", "");
}
