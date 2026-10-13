// ArgonUI - backends/common/ComPtr.h
//
// A minimal COM smart pointer.
//
// Why not Microsoft::WRL::ComPtr? Because <wrl/client.h> only ships with the
// Windows SDK. MinGW-w64 provides the header but not the class, so anything
// built on it silently stops compiling on a perfectly ordinary toolchain. The
// surface ArgonUI needs is four methods, so it is cheaper to own them.
//
// This is header-only and has no dependency beyond windows.h.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <utility>

namespace argon::ui {

/// Owning pointer to a COM interface. Move-only: copying a COM reference means
/// an AddRef/Release pair, and getting that wrong silently corrupts a refcount.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /// Takes ownership without AddRef. `p` must not already have a reference
    /// the caller intended to keep, or that reference leaks.
    void Attach(T* p) {
        if (p != ptr_) Reset();
        ptr_ = p;
    }

    /// Releases the current reference and adopts `p`, AddRef-ing it first.
    void Reset(T* p = nullptr) {
        if (ptr_ != nullptr) ptr_->Release();
        ptr_ = p;
    }

    /// Hands the reference back to the caller, who becomes responsible for it.
    T* Detach() {
        T* p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    T* Get() const { return ptr_; }
    /// Out-param form for COM entry points that take `ID3D12Foo**`.
    T** GetAddressOf() { return &ptr_; }
    /// Out-param form for COM entry points that take `void**`.
    void** GetAddressOfVoid() { return reinterpret_cast<void**>(&ptr_); }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    operator bool() const { return ptr_ != nullptr; }

    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    /// Assigning a raw pointer adopts an existing reference, matching WRL. Use
    /// Adopt() when the pointer came from a raw AddRef.
    ComPtr& operator=(T* p) {
        if (p != ptr_) Reset(p);
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) {
        Reset();
        return *this;
    }

    void Swap(ComPtr& other) noexcept { std::swap(ptr_, other.ptr_); }

    /// Adopts a reference the caller already AddRef'd, e.g. out-param results.
    static ComPtr Adopt(T* p) {
        ComPtr c;
        c.ptr_ = p;
        return c;
    }

    /// QueryInterface to a different interface on the same object. Returns
    /// S_FALSE and leaves `out` empty when the object does not implement it.
    /// Takes a pointer, matching the WRL spelling call sites already use.
    template <typename U>
    HRESULT As(ComPtr<U>* out) const {
        if (out == nullptr) return E_POINTER;
        out->Reset();
        if (ptr_ == nullptr) return E_POINTER;
        return ptr_->QueryInterface(__uuidof(U), out->GetAddressOfVoid());
    }

private:
    T* ptr_ = nullptr;
};

} // namespace argon::ui