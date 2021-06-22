// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @file cmd/cli.cc

#include <functional>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace ax {

struct ParamBase
{
    ParamBase(const char* name, const char* doc) : mName(name), mDoc(doc) {}
    inline const char* name() const { return mName; }
    inline const char* doc() const { return mDoc; }
    virtual void str(std::ostream&) const = 0;
    virtual bool init(const char* arg) = 0;
    virtual bool isInit() const = 0;
private:
    const char* const mName;
    const char* const mDoc;
};

template <typename T>
struct BasicParam {
    using CB = std::function<void(T&, const char*)>;
    BasicParam(const T& v) : mValue(v) {}
    BasicParam(const T& v, const char*, const char*, const CB&) : BasicParam(v) {}
    inline const T& operator=(const T& other) { mValue = other; return mValue; }
    inline void set(const T& v) { mValue = v; }
    inline T& get() { return mValue; }
    inline const T& get() const { return mValue; }
    inline operator const T&() const { return mValue; }
protected:
    T mValue;
};

template <typename T>
struct CLIParam final : public BasicParam<T>, ParamBase
{
    using CB = std::function<void(T&, const char*)>;
    CLIParam(const T& v, const char* name, const char* doc, const CB& cb)
        : BasicParam<T>(v), ParamBase(name, doc), mCb(cb), mInit(false) {}
    inline void str(std::ostream& os) const override final { os << BasicParam<T>::mValue; }
    inline bool init(const char* arg) override final {
        try { mCb(BasicParam<T>::mValue, arg); mInit = true; } catch(...) { mInit = false; }
        return mInit;
    }
    inline bool isInit() const { return mInit; }
private:
    CB mCb;
    bool mInit;
};

} // namespace ax
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb
