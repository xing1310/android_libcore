/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "NativeBreakIterator"

#include "IcuUtilities.h"
#include "JNIHelp.h"
#include "JniConstants.h"
#include "JniException.h"
#include "ScopedUtfChars.h"
#include "unicode/brkiter.h"
#include "unicode/putil.h"
#include <stdlib.h>

// ICU documentation: http://icu-project.org/apiref/icu4c/classBreakIterator.html

static BreakIterator* toBreakIterator(jint address) {
  return reinterpret_cast<BreakIterator*>(static_cast<uintptr_t>(address));
}

/**
 * We use ICU4C's BreakIterator class, but our input is on the Java heap and potentially moving
 * around between calls. This wrapper class ensures that our RegexMatcher is always pointing at
 * the current location of the char[]. Earlier versions of Android simply copied the data to the
 * native heap, but that's wasteful and hides allocations from the garbage collector.
 */
class BreakIteratorAccessor {
 public:
  BreakIteratorAccessor(JNIEnv* env, jint address, jstring javaInput, bool reset) {
    init(env, address);
    mJavaInput = javaInput;

    if (mJavaInput == NULL) {
      return;
    }

    mChars = env->GetStringChars(mJavaInput, NULL);
    if (mChars == NULL) {
      return;
    }

    mUText = utext_openUChars(NULL, mChars, env->GetStringLength(mJavaInput), &mStatus);
    if (mUText == NULL) {
      return;
    }

    if (reset) {
      mBreakIterator->setText(mUText, mStatus);
    } else {
      mBreakIterator->refreshInputText(mUText, mStatus);
    }
  }

  BreakIteratorAccessor(JNIEnv* env, jint address) {
    init(env, address);
  }

  ~BreakIteratorAccessor() {
    utext_close(mUText);
    if (mJavaInput) {
      mEnv->ReleaseStringChars(mJavaInput, mChars);
    }
    maybeThrowIcuException(mEnv, "utext_close", mStatus);
  }

  BreakIterator* operator->() {
    return mBreakIterator;
  }

  UErrorCode& status() {
    return mStatus;
  }

 private:
  void init(JNIEnv* env, jint address) {
    mEnv = env;
    mJavaInput = NULL;
    mBreakIterator = toBreakIterator(address);
    mChars = NULL;
    mStatus = U_ZERO_ERROR;
    mUText = NULL;
  }

  JNIEnv* mEnv;
  jstring mJavaInput;
  BreakIterator* mBreakIterator;
  const jchar* mChars;
  UErrorCode mStatus;
  UText* mUText;

  // Disallow copy and assignment.
  BreakIteratorAccessor(const BreakIteratorAccessor&);
  void operator=(const BreakIteratorAccessor&);
};

#define MAKE_BREAK_ITERATOR_INSTANCE(F) \
  UErrorCode status = U_ZERO_ERROR; \
  const ScopedUtfChars localeChars(env, javaLocale); \
  if (localeChars.c_str() == NULL) { \
    return 0; \
  } \
  Locale locale(Locale::createFromName(localeChars.c_str())); \
  BreakIterator* it = F(locale, status); \
  if (maybeThrowIcuException(env, "ubrk_open", status)) { \
    return 0; \
  } \
  return reinterpret_cast<uintptr_t>(it)

static jint NativeBreakIterator_cloneImpl(JNIEnv* env, jclass, jint address) {
  BreakIteratorAccessor it(env, address);
  return reinterpret_cast<uintptr_t>(it->clone());
}

static void NativeBreakIterator_closeImpl(JNIEnv*, jclass, jint address) {
  delete toBreakIterator(address);
}

static jint NativeBreakIterator_currentImpl(JNIEnv* env, jclass, jint address, jstring javaInput) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->current();
}

static jint NativeBreakIterator_firstImpl(JNIEnv* env, jclass, jint address, jstring javaInput) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->first();
}

static jint NativeBreakIterator_followingImpl(JNIEnv* env, jclass, jint address, jstring javaInput, jint offset) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->following(offset);
}

static jint NativeBreakIterator_getCharacterInstanceImpl(JNIEnv* env, jclass, jstring javaLocale) {
  MAKE_BREAK_ITERATOR_INSTANCE(BreakIterator::createCharacterInstance);
}

static jint NativeBreakIterator_getLineInstanceImpl(JNIEnv* env, jclass, jstring javaLocale) {
  MAKE_BREAK_ITERATOR_INSTANCE(BreakIterator::createLineInstance);
}

static jint NativeBreakIterator_getSentenceInstanceImpl(JNIEnv* env, jclass, jstring javaLocale) {
  MAKE_BREAK_ITERATOR_INSTANCE(BreakIterator::createSentenceInstance);
}

static jint NativeBreakIterator_getWordInstanceImpl(JNIEnv* env, jclass, jstring javaLocale) {
  MAKE_BREAK_ITERATOR_INSTANCE(BreakIterator::createWordInstance);
}

static jboolean NativeBreakIterator_isBoundaryImpl(JNIEnv* env, jclass, jint address, jstring javaInput, jint offset) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->isBoundary(offset);
}

static jint NativeBreakIterator_lastImpl(JNIEnv* env, jclass, jint address, jstring javaInput) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->last();
}

static jint NativeBreakIterator_nextImpl(JNIEnv* env, jclass, jint address, jstring javaInput, jint n) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  if (n < 0) {
    while (n++ < -1) {
      it->previous();
    }
    return it->previous();
  } else if (n == 0) {
    return it->current();
  } else {
    while (n-- > 1) {
      it->next();
    }
    return it->next();
  }
  return -1;
}

static jint NativeBreakIterator_precedingImpl(JNIEnv* env, jclass, jint address, jstring javaInput, jint offset) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->preceding(offset);
}

static jint NativeBreakIterator_previousImpl(JNIEnv* env, jclass, jint address, jstring javaInput) {
  BreakIteratorAccessor it(env, address, javaInput, false);
  return it->previous();
}

static void NativeBreakIterator_setTextImpl(JNIEnv* env, jclass, jint address, jstring javaInput) {
  BreakIteratorAccessor it(env, address, javaInput, true);
}

static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(NativeBreakIterator, cloneImpl, "(I)I"),
  NATIVE_METHOD(NativeBreakIterator, closeImpl, "(I)V"),
  NATIVE_METHOD(NativeBreakIterator, currentImpl, "(ILjava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, firstImpl, "(ILjava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, followingImpl, "(ILjava/lang/String;I)I"),
  NATIVE_METHOD(NativeBreakIterator, getCharacterInstanceImpl, "(Ljava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, getLineInstanceImpl, "(Ljava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, getSentenceInstanceImpl, "(Ljava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, getWordInstanceImpl, "(Ljava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, isBoundaryImpl, "(ILjava/lang/String;I)Z"),
  NATIVE_METHOD(NativeBreakIterator, lastImpl, "(ILjava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, nextImpl, "(ILjava/lang/String;I)I"),
  NATIVE_METHOD(NativeBreakIterator, precedingImpl, "(ILjava/lang/String;I)I"),
  NATIVE_METHOD(NativeBreakIterator, previousImpl, "(ILjava/lang/String;)I"),
  NATIVE_METHOD(NativeBreakIterator, setTextImpl, "(ILjava/lang/String;)V"),
};
void register_libcore_icu_NativeBreakIterator(JNIEnv* env) {
  jniRegisterNativeMethods(env, "libcore/icu/NativeBreakIterator", gMethods, NELEM(gMethods));
}
