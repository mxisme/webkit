/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#pragma once

#include "JSDOMBinding.h"
#include "JSDOMConvert.h"
#include "JSDOMGlobalObject.h"
#include <JavaScriptCore/BuiltinNames.h>
#include <JavaScriptCore/CommonIdentifiers.h>

namespace WebCore {

WEBCORE_EXPORT std::pair<bool, std::reference_wrapper<JSC::JSObject>> getBackingSet(JSC::JSGlobalObject&, JSC::JSObject& setLike);
WEBCORE_EXPORT JSC::JSValue forwardAttributeGetterToBackingSet(JSC::JSGlobalObject&, JSC::JSObject&, const JSC::Identifier&);
WEBCORE_EXPORT JSC::JSValue forwardFunctionCallToBackingSet(JSC::JSGlobalObject&, JSC::CallFrame&, JSC::JSObject&, const JSC::Identifier&);
WEBCORE_EXPORT void clearBackingSet(JSC::JSGlobalObject&, JSC::JSObject&);
WEBCORE_EXPORT void addToBackingSet(JSC::JSGlobalObject&, JSC::JSObject&, JSC::JSValue item);

template<typename WrapperClass> JSC::JSObject& getAndInitializeBackingSet(JSC::JSGlobalObject&, WrapperClass&);
template<typename WrapperClass> JSC::JSValue forwardSizeToSetLike(JSC::JSGlobalObject&, WrapperClass&);
template<typename WrapperClass> JSC::JSValue forwardEntriesToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&);
template<typename WrapperClass> JSC::JSValue forwardKeysToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&);
template<typename WrapperClass> JSC::JSValue forwardValuesToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&);
template<typename WrapperClass> JSC::JSValue forwardClearToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&);
template<typename WrapperClass, typename ItemType> JSC::JSValue forwardHasToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&, ItemType&&);
template<typename WrapperClass, typename ItemType> JSC::JSValue forwardAddToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&, ItemType&&);
template<typename WrapperClass, typename ItemType> JSC::JSValue forwardDeleteToSetLike(JSC::JSGlobalObject&, JSC::CallFrame&, WrapperClass&, ItemType&&);

class DOMSetAdapter {
public:
    DOMSetAdapter(JSC::JSGlobalObject&, JSC::JSObject&);
    template<class IDLType> void add(typename IDLType::ParameterType value);
    void clear();

private:
    JSC::JSGlobalObject& m_lexicalGlobalObject;
    JSC::JSObject& m_backingSet;
};

inline DOMSetAdapter::DOMSetAdapter(JSC::JSGlobalObject& lexicalGlobalObject, JSC::JSObject& backingSet)
    : m_lexicalGlobalObject(lexicalGlobalObject)
    , m_backingSet(backingSet)
{
}

template<class IDLType>
inline void DOMSetAdapter::add(typename IDLType::ParameterType value)
{
    JSC::JSLockHolder locker(&m_lexicalGlobalObject);
    auto item = toJS<IDLType>(m_lexicalGlobalObject, *JSC::jsCast<JSDOMGlobalObject*>(&m_lexicalGlobalObject), std::forward<typename IDLType::ParameterType>(value));
    addToBackingSet(m_lexicalGlobalObject, m_backingSet, item);
}

inline void DOMSetAdapter::clear()
{
    clearBackingSet(m_lexicalGlobalObject, m_backingSet);
}

template<typename WrapperClass> inline JSC::JSObject& getAndInitializeBackingSet(JSC::JSGlobalObject& lexicalGlobalObject, WrapperClass& setLike)
{
    auto pair = getBackingSet(lexicalGlobalObject, setLike);
    if (pair.first) {
        DOMSetAdapter adapter { lexicalGlobalObject, pair.second.get() };
        setLike.wrapped().initializeSetLike(adapter);
    }
    return pair.second.get();
}

template<typename WrapperClass> inline JSC::JSValue forwardSizeToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, WrapperClass& setLike)
{
    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardAttributeGetterToBackingSet(lexicalGlobalObject, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->size);
}

template<typename WrapperClass> inline JSC::JSValue forwardEntriesToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike)
{
    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->builtinNames().entriesPublicName());
}

template<typename WrapperClass> inline JSC::JSValue forwardKeysToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike)
{
    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->builtinNames().keysPublicName());
}

template<typename WrapperClass> inline JSC::JSValue forwardValuesToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike)
{
    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->builtinNames().valuesPublicName());
}

template<typename WrapperClass, typename ItemType> inline JSC::JSValue forwardHasToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike, ItemType&&)
{
    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->builtinNames().hasPublicName());
}

template<typename WrapperClass> inline JSC::JSValue forwardClearToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike)
{
    setLike.wrapped().clearFromSetLike();

    auto& vm = JSC::getVM(&lexicalGlobalObject);
    return forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->clear);
}

template<typename WrapperClass, typename ItemType> inline JSC::JSValue forwardAddToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike, ItemType&& item)
{
    setLike.wrapped().addToSetLike(std::forward<ItemType>(item));

    auto& vm = JSC::getVM(&lexicalGlobalObject);
    forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->add);
    return &setLike;
}

template<typename WrapperClass, typename ItemType> inline JSC::JSValue forwardDeleteToSetLike(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, WrapperClass& setLike, ItemType&& item)
{
    auto isDeleted = setLike.wrapped().removeFromSetLike(std::forward<ItemType>(item));
    UNUSED_PARAM(isDeleted);

    auto& vm = JSC::getVM(&lexicalGlobalObject);
    auto result = forwardFunctionCallToBackingSet(lexicalGlobalObject, callFrame, getAndInitializeBackingSet(lexicalGlobalObject, setLike), vm.propertyNames->deleteKeyword);
    ASSERT_UNUSED(result, result.asBoolean() == isDeleted);
    return result;
}

}
