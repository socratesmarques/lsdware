#pragma once
#include "lsd/source2.hpp"
#include <windows.h>
namespace lsd {
class Process final : public Memory {
  public:
    explicit Process(bool allow_write);
    ~Process() override;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    bool read_bytes(Address address, void* out, std::size_t bytes) const override;
    bool write_angles(Address address, Angles angles) const;
    Module module(const wchar_t* name) const;
    HWND window() const;
    bool alive() const;
    std::string steam_build_id() const;
    bool writable() const {
        return allow_write_;
    }

  private:
    DWORD pid_{};
    HANDLE handle_{};
    bool allow_write_{};
};
} // namespace lsd
