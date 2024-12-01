#include "polyglat-shared/pch.h"
#include "polyglat-shared/annotation.h"

namespace {
    auto CreateAnnotation(const std::string &key, const std::string &value) -> std::string {
        std::stringstream ss;
        ss << polyglat::shared::Namespace << key << '=' << value;
        return ss.str();
    }

    void ApplyAnnotation(clang::NamedDecl *decl, const std::string &value) {
        if (dyn_cast<clang::TypeDecl>(decl)) {
            decl->addAttr(clang::AnnotateTypeAttr::Create(decl->getASTContext(), value, nullptr, 0));
        } else {
            decl->addAttr(clang::AnnotateAttr::Create(decl->getASTContext(), value, nullptr, 0));
        }
    }

    auto GetDirectory(const clang::NamedDecl *decl) -> std::string {
        std::stack<llvm::StringRef> namespaces;
        for (
            decl = dyn_cast<clang::NamedDecl>(decl->getDeclContext());
            decl;
            decl = dyn_cast<clang::NamedDecl>(decl->getDeclContext())) {
            if (auto name = decl->getName(); !name.empty()) {
                namespaces.push(name);
            }
        }

        std::stringstream ss;
        for (; !namespaces.empty(); namespaces.pop()) {
            ss << '/' << namespaces.top().str();
        }

        return ss.str();
    }

    class TypeID {
        std::string Namespace;
        std::string Name;
        size_t RefCnt;

      public:
        TypeID(std::string ns, std::string name, const size_t refCnt)
            : Namespace(std::move(ns)),
              Name(std::move(name)),
              RefCnt(refCnt) {
        }

        [[nodiscard]]
        auto str() const -> std::string {
            std::stringstream ss;
            ss << Namespace << "/" << Name << ',' << RefCnt;
            return ss.str();
        }
    };

    auto GetTypeID(const clang::Type *type) -> TypeID {
        size_t refCnt = 0;
        while (true) {
            if (const auto enumT = dyn_cast<clang::EnumType>(type)) {
                type = enumT->getDecl()->getIntegerType().getTypePtr();
            }

            if (const auto pointee = type->getPointeeType().getTypePtrOrNull()) {
                type = pointee;
                refCnt++;
                continue;
            }

            if (const auto array = dyn_cast<clang::ArrayType>(type)) {
                type = array->getArrayElementTypeNoTypeQual();
                refCnt++;
                continue;
            }

            break;
        }

        if (type->isBuiltinType()) {
            auto builtin = dyn_cast<clang::BuiltinType>(type);

            const static std::map<clang::BuiltinType::Kind, std::string> types = {
#define LILAC_TYPE(type, name) { clang::BuiltinType::Kind::type, #name }
//===- Builtin Types ------------------------------------------------------===//
                LILAC_TYPE(Void, void),
//===- Code-Point Types ---------------------------------------------------===//
                LILAC_TYPE(Char8, cp8),
                LILAC_TYPE(Char16, cp16),
                LILAC_TYPE(Char32, cp32),
                LILAC_TYPE(WChar_U, cp16),
                LILAC_TYPE(WChar_S, cp16),
//===- Unsigned Types -----------------------------------------------------===//
                LILAC_TYPE(Bool, bool),
                LILAC_TYPE(Char_U, u1),
                LILAC_TYPE(UChar, u1),
                LILAC_TYPE(UShort, u2),
                LILAC_TYPE(UInt, u4),
                LILAC_TYPE(ULong, up),
                LILAC_TYPE(ULongLong, u8),
                LILAC_TYPE(UInt128, u16),
//===- Signed Types -------------------------------------------------------===//
                LILAC_TYPE(Char_S, s1),
                LILAC_TYPE(SChar, s1),
                LILAC_TYPE(Short, s2),
                LILAC_TYPE(Int, s4),
                LILAC_TYPE(Long, sp),
                LILAC_TYPE(LongLong, s8),
                LILAC_TYPE(Int128, s16),
//===- Floating point types -----------------------------------------------===//
                LILAC_TYPE(Half, fp2),
                LILAC_TYPE(Float16, fp2),
                LILAC_TYPE(Float, fp4),
                LILAC_TYPE(Double, fp8),
#undef LILAC_TYPE
            };

            return { "__builtin__", types.at(builtin->getKind()), refCnt };
        }

        const auto decl = type->getAsRecordDecl();
        if (!decl) {
            throw std::runtime_error("Could not find record decl");
        }

        return {
            GetDirectory(decl),
            decl->getNameAsString(),
            refCnt,
        };
    }

    auto ParseTypeID(const std::string &id) -> std::optional<TypeID> {
        constexpr int BASE10 = 10;

        auto delimiter = id.rfind('/');
        if (delimiter == std::string::npos) {
            return std::nullopt;
        }

        const auto ns = id.substr(0, delimiter);
        auto name = id.substr(delimiter + 1);

        delimiter = name.find(',');
        const auto refCntStr = name.substr(delimiter + 1);
        name = name.substr(0, delimiter);

        const auto refCnt = std::strtoul(refCntStr.c_str(), nullptr, BASE10);

        return TypeID{ ns, name, refCnt };
    }

    auto GetCallConv(const clang::FunctionDecl *fn) -> std::string {
        const auto cc = fn->getFunctionType()->getCallConv();
        return clang::FunctionType::getNameForCallConv(cc).str();
    }
} // namespace

void polyglat::shared::MarkAsTarget(clang::NamedDecl *decl) {
    ApplyAnnotation(decl, Namespace);
}

void polyglat::shared::CreateAnnotation(clang::NamedDecl *decl) {
    if (const auto typeDecl = dyn_cast<clang::TypeDecl>(decl)) {
        constexpr size_t BIT_PER_BYTE = 8;

        const auto info = typeDecl->getASTContext().getTypeInfo(typeDecl->getTypeForDecl());
        const auto size = info.Width / BIT_PER_BYTE;
        const auto align = info.Align / BIT_PER_BYTE;

        ApplyAnnotation(decl, ::CreateAnnotation(NAMESPACE, GetDirectory(decl)));
        ApplyAnnotation(decl, ::CreateAnnotation(NAME, decl->getNameAsString()));
        ApplyAnnotation(decl, ::CreateAnnotation(SIZE, std::to_string(size)));
        ApplyAnnotation(decl, ::CreateAnnotation(ALIGN, std::to_string(align)));
    } else if (const auto funcDecl = dyn_cast<clang::FunctionDecl>(decl)) {
        ApplyAnnotation(decl, ::CreateAnnotation(NAMESPACE, GetDirectory(decl)));
        ApplyAnnotation(decl, ::CreateAnnotation(NAME, decl->getNameAsString()));
        ApplyAnnotation(decl, ::CreateAnnotation(CALLCONV, GetCallConv(funcDecl)));
        ApplyAnnotation(decl, ::CreateAnnotation(RETURN, GetTypeID(funcDecl->getReturnType().getTypePtr()).str()));
    } else if (const auto paramDecl = dyn_cast<clang::ParmVarDecl>(decl)) {
        ApplyAnnotation(decl, ::CreateAnnotation(NAME, decl->getNameAsString()));
        ApplyAnnotation(decl, ::CreateAnnotation(TYPE, GetTypeID(paramDecl->getType().getTypePtr()).str()));
    }
}

auto polyglat::shared::ParseAnnotation(const std::string &annotation) -> std::optional<std::pair<std::string, std::string>> {
    if (!annotation.starts_with(Namespace) || annotation == Namespace) {
        return std::nullopt;
    }

    const auto line = annotation.substr(Namespace.size());
    const auto delimiter = line.find('=');

    return std::make_pair(line.substr(0, delimiter), line.substr(delimiter + 1));
}
