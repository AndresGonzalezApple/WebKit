/*
 * Copyright (C) 2009-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#include "BatchedTransitionOptimizer.h"
#include "CodeCache.h"
#include "Debugger.h"
#include "VMTrapsInlines.h"
#include <wtf/text/MakeString.h>

#if PLATFORM(COCOA)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

namespace JSC {

const ClassInfo ProgramExecutable::s_info = { "ProgramExecutable"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(ProgramExecutable) };

ProgramExecutable::ProgramExecutable(JSGlobalObject* globalObject, const SourceCode& source)
    : Base(globalObject->vm().programExecutableStructure.get(), globalObject->vm(), source, NoLexicallyScopedFeatures, DerivedContextType::None, false, false, EvalContextType::None, NoIntrinsic)
{
    ASSERT(source.provider()->sourceType() == SourceProviderSourceType::Program);
    VM& vm = globalObject->vm();
    if (vm.typeProfiler() || vm.controlFlowProfiler())
        vm.functionHasExecutedCache()->insertUnexecutedRange(sourceID(), typeProfilingStartOffset(), typeProfilingEndOffset());
}

void ProgramExecutable::destroy(JSCell* cell)
{
    static_cast<ProgramExecutable*>(cell)->ProgramExecutable::~ProgramExecutable();
}

// http://www.ecma-international.org/ecma-262/6.0/index.html#sec-hasrestrictedglobalproperty
enum class GlobalPropertyLookUpStatus {
    NotFound,
    Configurable,
    NonConfigurable,
};
static GlobalPropertyLookUpStatus hasRestrictedGlobalProperty(JSGlobalObject* globalObject, PropertyName propertyName)
{
    PropertyDescriptor descriptor;
    if (!globalObject->getOwnPropertyDescriptor(globalObject, propertyName, descriptor))
        return GlobalPropertyLookUpStatus::NotFound;
    if (descriptor.configurable())
        return GlobalPropertyLookUpStatus::Configurable;
    return GlobalPropertyLookUpStatus::NonConfigurable;
}

static ALWAYS_INLINE bool requiresCanDeclareGlobalFunctionQuirk()
{
#if PLATFORM(COCOA)
    // https://bugs.webkit.org/show_bug.cgi?id=267199
    static bool requiresQuirk = !linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::ThrowIfCanDeclareGlobalFunctionFails);
    return requiresQuirk;
#else
    return false;
#endif
}

JSObject* ProgramExecutable::initializeGlobalProperties(VM& vm, JSGlobalObject* globalObject, JSScope* scope)
{
    DeferTermination deferScope(vm);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    RELEASE_ASSERT(scope);
    ASSERT(globalObject == scope->globalObject());
    RELEASE_ASSERT(globalObject);
    ASSERT(&globalObject->vm() == &vm);

    ParserError error;
    OptionSet<CodeGenerationMode> codeGenerationMode = globalObject->defaultCodeGenerationMode();
    UnlinkedProgramCodeBlock* unlinkedCodeBlock = vm.codeCache()->getUnlinkedProgramCodeBlock(
        vm, this, source(), codeGenerationMode, error);

    if (globalObject->hasDebugger())
        globalObject->debugger()->sourceParsed(globalObject, source().provider(), error.line(), error.message());

    if (error.isValid())
        return error.toErrorObject(globalObject, source());

    JSValue nextPrototype = globalObject->getPrototypeDirect();
    while (nextPrototype && nextPrototype.isObject()) {
        if (asObject(nextPrototype)->type() == ProxyObjectType) [[unlikely]]
            return createTypeError(globalObject, "Proxy is not allowed in the global prototype chain."_s);
        nextPrototype = asObject(nextPrototype)->getPrototypeDirect();
    }
    
    JSGlobalLexicalEnvironment* globalLexicalEnvironment = globalObject->globalLexicalEnvironment();
    bool hasGlobalLexicalDeclarations = !globalLexicalEnvironment->isEmpty();
    const VariableEnvironment& variableDeclarations = unlinkedCodeBlock->variableDeclarations();
    const VariableEnvironment& lexicalDeclarations = unlinkedCodeBlock->lexicalDeclarations();
    size_t numberOfFunctions = unlinkedCodeBlock->numberOfFunctionDecls();
    // The ES6 spec says that no vars/global properties/let/const can be duplicated in the global scope.
    // This carried out section 15.1.8 of the ES6 spec: http://www.ecma-international.org/ecma-262/6.0/index.html#sec-globaldeclarationinstantiation
    {
        // Check for intersection of "var" and "let"/"const"/"class"
        // Check if any new "let"/"const"/"class" will shadow any pre-existing global property names (with configurable = false), or "var"/"let"/"const" variables.
        // It's an error to introduce a shadow.
        for (auto& entry : lexicalDeclarations) {
            if (hasGlobalLexicalDeclarations) {
                bool hasProperty = globalLexicalEnvironment->hasProperty(globalObject, entry.key.get());
                throwScope.assertNoExceptionExceptTermination();
                if (hasProperty) {
                    if (entry.value.isConst() && !vm.globalConstRedeclarationShouldThrow() && !isInStrictContext()) [[unlikely]] {
                        // We only allow "const" duplicate declarations under this setting.
                        // For example, we don't allow "let" variables to be overridden by "const" variables.
                        if (globalLexicalEnvironment->isConstVariable(entry.key.get()))
                            continue;
                    }
                    return createErrorForDuplicateGlobalVariableDeclaration(globalObject, entry.key.get());
                }
            }

            // The ES6 spec says that RestrictedGlobalProperty can't be shadowed.
            GlobalPropertyLookUpStatus status = hasRestrictedGlobalProperty(globalObject, entry.key.get());
            RETURN_IF_EXCEPTION(throwScope, nullptr);
            switch (status) {
            case GlobalPropertyLookUpStatus::NonConfigurable:
                return createSyntaxError(globalObject, makeString("Can't create duplicate variable that shadows a global property: '"_s, StringView(entry.key.get()), '\''));
            case GlobalPropertyLookUpStatus::Configurable:
                // Lexical bindings can shadow global properties if the given property's attribute is configurable.
                // https://tc39.github.io/ecma262/#sec-globaldeclarationinstantiation step 5-c, `hasRestrictedGlobal` becomes false
                // However we may emit GlobalProperty look up in bytecodes already and it may cache the value for the global scope.
                // To make it invalid,
                // 1. In LLInt and Baseline, we bump the global lexical binding epoch and it works.
                // 3. In DFG and FTL, we watch the watchpoint and jettison once it is fired.
                break;
            case GlobalPropertyLookUpStatus::NotFound:
                break;
            }
        }

        // Check if any new "var"s will shadow any previous "let"/"const"/"class" names.
        // It's an error to introduce a shadow.
        if (hasGlobalLexicalDeclarations) {
            for (auto& entry : variableDeclarations) {
                if (entry.value.isSloppyModeHoistedFunction())
                    continue;
                bool hasProperty = globalLexicalEnvironment->hasProperty(globalObject, entry.key.get());
                throwScope.assertNoExceptionExceptTermination();
                if (hasProperty)
                    return createErrorForDuplicateGlobalVariableDeclaration(globalObject, entry.key.get());
            }
        }

        for (size_t i = 0; i < numberOfFunctions; ++i) {
            UnlinkedFunctionExecutable* unlinkedFunctionExecutable = unlinkedCodeBlock->functionDecl(i);
            ASSERT(!unlinkedFunctionExecutable->name().isEmpty());
            bool canDeclare = globalObject->canDeclareGlobalFunction(unlinkedFunctionExecutable->name());
            throwScope.assertNoExceptionExceptTermination();
            if (!canDeclare) {
                if (requiresCanDeclareGlobalFunctionQuirk()) {
                    VM::DeletePropertyModeScope scope(vm, VM::DeletePropertyMode::IgnoreConfigurable);
                    JSCell::deleteProperty(globalObject, globalObject, unlinkedFunctionExecutable->name());
                    throwScope.assertNoExceptionExceptTermination();
                    continue;
                }
                return createErrorForInvalidGlobalFunctionDeclaration(globalObject, unlinkedFunctionExecutable->name());
            }
        }

        if (!globalObject->isStructureExtensible()) {
            for (auto& entry : variableDeclarations) {
                if (entry.value.isFunction() || entry.value.isSloppyModeHoistedFunction())
                    continue;
                ASSERT(entry.value.isVar());
                const Identifier& ident = Identifier::fromUid(vm, entry.key.get());
                bool canDeclare = globalObject->canDeclareGlobalVar(ident);
                throwScope.assertNoExceptionExceptTermination();
                if (!canDeclare)
                    return createErrorForInvalidGlobalVarDeclaration(globalObject, ident);
            }
        }
    }

    m_unlinkedCodeBlock.set(vm, this, unlinkedCodeBlock);

    BatchedTransitionOptimizer optimizer(vm, globalObject);

    // https://tc39.es/ecma262/#sec-web-compat-globaldeclarationinstantiation (excluding last step)
    if (!isInStrictContext()) {
        for (auto& entry : variableDeclarations) {
            if (!entry.value.isSloppyModeHoistedFunction())
                continue;

            const Identifier& ident = Identifier::fromUid(vm, entry.key.get());

            if (hasGlobalLexicalDeclarations) {
                bool hasProperty = globalLexicalEnvironment->hasProperty(globalObject, ident);
                throwScope.assertNoExceptionExceptTermination();
                if (hasProperty)
                    continue;
            }

            bool canDeclare = globalObject->canDeclareGlobalVar(ident);
            throwScope.assertNoExceptionExceptTermination();
            if (!canDeclare)
                continue;

            globalObject->createGlobalVarBinding<BindingCreationContext::Global>(ident);
            throwScope.assertNoExceptionExceptTermination();
        }
    }

    for (size_t i = 0; i < numberOfFunctions; ++i) {
        UnlinkedFunctionExecutable* unlinkedFunctionExecutable = unlinkedCodeBlock->functionDecl(i);
        ASSERT(!unlinkedFunctionExecutable->name().isEmpty());
        globalObject->createGlobalFunctionBinding<BindingCreationContext::Global>(unlinkedFunctionExecutable->name());
        throwScope.assertNoExceptionExceptTermination();
        if (vm.typeProfiler() || vm.controlFlowProfiler()) {
            vm.functionHasExecutedCache()->insertUnexecutedRange(sourceID(), 
                unlinkedFunctionExecutable->unlinkedFunctionStart(),
                unlinkedFunctionExecutable->unlinkedFunctionEnd());
        }
    }

    for (auto& entry : variableDeclarations) {
        if (entry.value.isFunction() || entry.value.isSloppyModeHoistedFunction())
            continue;
        ASSERT(entry.value.isVar());
        globalObject->createGlobalVarBinding<BindingCreationContext::Global>(Identifier::fromUid(vm, entry.key.get()));
        throwScope.assertNoExceptionExceptTermination();
    }

    {
        SymbolTable* symbolTable = globalLexicalEnvironment->symbolTable();
        ConcurrentJSLocker locker(symbolTable->m_lock);
        for (auto& entry : lexicalDeclarations) {
            if (entry.value.isConst() && !vm.globalConstRedeclarationShouldThrow() && !isInStrictContext()) [[unlikely]] {
                if (symbolTable->contains(locker, entry.key.get()))
                    continue;
            }
            ScopeOffset offset = symbolTable->takeNextScopeOffset(locker);
            SymbolTableEntry newEntry(VarOffset(offset), static_cast<unsigned>(entry.value.isConst() ? PropertyAttribute::ReadOnly : PropertyAttribute::None));
            newEntry.prepareToWatch();
            symbolTable->add(locker, entry.key.get(), newEntry);
            
            ScopeOffset offsetForAssert = globalLexicalEnvironment->addVariables(1, jsTDZValue());
            RELEASE_ASSERT(offsetForAssert == offset);
        }
    }
    if (lexicalDeclarations.size()) {
#if ENABLE(DFG_JIT)
        for (auto& entry : lexicalDeclarations) {
            // If WatchpointSet exists, just fire it. Since DFG WatchpointSet addition is also done on the main thread, we can sync them.
            // So that we do not create WatchpointSet here. DFG will create if necessary on the main thread.
            // And it will only create not-invalidated watchpoint set if the global lexical environment binding doesn't exist, which is why this code works.
            if (auto* watchpointSet = globalObject->getReferencedPropertyWatchpointSet(entry.key.get()))
                watchpointSet->fireAll(vm, "Lexical binding shadows an existing global property");
        }
#endif
        globalObject->bumpGlobalLexicalBindingEpoch(vm);
    }
    return nullptr;
}

auto ProgramExecutable::ensureTemplateObjectMap(VM&) -> TemplateObjectMap&
{
    return ensureTemplateObjectMapImpl(m_templateObjectMap);
}

template<typename Visitor>
void ProgramExecutable::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    ProgramExecutable* thisObject = jsCast<ProgramExecutable*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    if (TemplateObjectMap* map = thisObject->m_templateObjectMap.get()) {
        Locker locker { thisObject->cellLock() };
        for (auto& entry : *map)
            visitor.append(entry.value);
    }
}

DEFINE_VISIT_CHILDREN(ProgramExecutable);

} // namespace JSC
