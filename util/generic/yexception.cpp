#include "type_name.h"
#include "yexception.h"

#include <util/folder/dirut.h>
#include <util/system/backtrace.h>

#include <string>

#include <stdexcept>

#include <cstdio>
#include <cstdarg>

template <class E>
static inline TString FormatExc(const E& e) {
    return TString::Join(AsStringBuf("("), TypeName(&e), AsStringBuf(") "), e.what());
}

TString CurrentExceptionMessage() {
    auto exceptionPtr = std::current_exception();
    if (exceptionPtr) {
        try {
            std::rethrow_exception(exceptionPtr);
        } catch (const yexception& e) {
            const TBackTrace* bt = e.BackTrace();

            if (bt) {
                return TString::Join(bt->PrintToString(), AsStringBuf("\n"), FormatExc(e));
            }

            return FormatExc(e);
        } catch (const std::exception& e) {
            return FormatExc(e);
        } catch (...) {
        }

        return "unknown error";
    }

    return "(NO EXCEPTION)";
}

bool UncaughtException() noexcept {
    return std::uncaught_exception();
}

void ThrowBadAlloc() {
    throw std::bad_alloc();
}

void ThrowLengthError(const char* descr) {
    throw std::length_error(descr);
}

void ThrowRangeError(const char* descr) {
    throw std::out_of_range(descr);
}

void TSystemError::Init() {
    yexception& exc = *this;

    exc << AsStringBuf("(");
    exc << TStringBuf(LastSystemErrorText(Status()));
    exc << AsStringBuf(") ");
}

static inline const char* ZeroTerminate(TTempBuf& buf) {
    char* end = (char*)buf.Current();

    if (!buf.Left()) {
        --end;
    }

    *end = 0;

    return buf.Data();
}

const char* NPrivateException::yexception::what() const noexcept {
    return ZeroTerminate(Buf_);
}

const TBackTrace* NPrivateException::yexception::BackTrace() const noexcept {
    return nullptr;
}

void fputs(const std::exception& e, FILE* f) {
    char message[256];
    size_t len = Min(sizeof(message) - 2, strlcpy(message, e.what(), sizeof(message) - 1));
    message[len++] = '\n';
    message[len] = 0;
    fputs(message, f);
}

void ::NPrivate::ThrowYException(const ::NPrivate::TSimpleExceptionMessage& sm) {
    throw sm.Location + yexception() << sm.Message;
}
