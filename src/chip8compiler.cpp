#include <chiplet/chip8compiler.hpp>
//#include <emulation/utility.hpp>

#include <chiplet/sha1.hpp>
#include <iostream>
#include <vector>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wnarrowing"
#pragma GCC diagnostic ignored "-Wc++11-narrowing"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma GCC diagnostic ignored "-Wenum-compare"
#pragma GCC diagnostic ignored "-Wwritable-strings"
#pragma GCC diagnostic ignored "-Wwrite-strings"
#if __clang__
#pragma GCC diagnostic ignored "-Wenum-compare-conditional"
#endif
#endif  // __GNUC__

extern "C" {
#include "c-octo/octo_compiler.h"
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__

namespace emu {

class Chip8Compiler::Private
{
public:
    octo_program* _program{nullptr};
    std::string _sha1hex;
    std::string _errorMessage;
    std::vector<std::pair<uint32_t, uint32_t>> _lineCoverage;
};

Chip8Compiler::Chip8Compiler()
    : _impl(new Private)
{
    _impl->_program = nullptr;
}

Chip8Compiler::~Chip8Compiler()
{
    if (_impl->_program) {
        octo_free_program(_impl->_program);
        _impl->_program = nullptr;
    }
}

bool Chip8Compiler::compile(std::string str, int startAddress)
{
    return compile(str.data(), str.data() + str.size() + 1, startAddress);
}

bool Chip8Compiler::compile(const char* start, const char* end, int startAddress)
{
    if (_impl->_program) {
        octo_free_program(_impl->_program);
        _impl->_program = nullptr;
    }
    // make a malloc based copy that c-octo will own and free on oct_free_program
    char* source = (char*)malloc(end - start);
    memcpy(source, start, end - start);
    _impl->_program = octo_compile_str(source, startAddress);
    if (!_impl->_program) {
        _impl->_errorMessage = "ERROR: unknown error, no binary generated";
    }
    else if (_impl->_program->is_error) {
        _impl->_errorMessage = "ERROR (" + std::to_string(_impl->_program->error_line) + ":" + std::to_string(_impl->_program->error_pos + 1) + "): " + _impl->_program->error;
        //std::cerr << _impl->_errorMessage << std::endl;
    }
    else {
        updateHash(); //calculateSha1Hex(code(), codeSize());
        _impl->_errorMessage = "No errors.";
        //std::clog << "compiled successfully." << std::endl;
    }
    return !_impl->_program->is_error;
}

std::string Chip8Compiler::rawErrorMessage() const
{
    if(!_impl->_program)
        return "unknown error";
    if(_impl->_program->is_error)
        return _impl->_program->error;
    return "";
}

int Chip8Compiler::errorLine() const
{
    return _impl->_program ? _impl->_program->error_line + 1 : 0;
}

int Chip8Compiler::errorCol() const
{
    return _impl->_program ? _impl->_program->error_pos + 1 : 0;
}

bool Chip8Compiler::isError() const
{
    return !_impl->_program || _impl->_program->is_error;
}

const std::string& Chip8Compiler::errorMessage() const
{
    return _impl->_errorMessage;
}

uint16_t Chip8Compiler::codeSize() const
{
    return _impl->_program && !_impl->_program->is_error ? _impl->_program->length - _impl->_program->startAddress : 0;
}

const uint8_t* Chip8Compiler::code() const
{
    return reinterpret_cast<const uint8_t*>(_impl->_program->rom + _impl->_program->startAddress);
}

const std::string& Chip8Compiler::sha1Hex() const
{
    return _impl->_sha1hex;
}

std::pair<uint32_t, uint32_t> Chip8Compiler::addrForLine(uint32_t line) const
{
    return line < _impl->_lineCoverage.size() && !isError() ? _impl->_lineCoverage[line] : std::make_pair(0xFFFFFFFFu, 0xFFFFFFFFu);
}

uint32_t Chip8Compiler::lineForAddr(uint32_t addr) const
{
    return addr < OCTO_RAM_MAX && !isError() ? _impl->_program->romLineMap[addr] : 0xFFFFFFFF;
}

const char* Chip8Compiler::breakpointForAddr(uint32_t addr) const
{
    if(addr < OCTO_RAM_MAX && _impl->_program->breakpoints[addr]) {
        return _impl->_program->breakpoints[addr];
    }
    return nullptr;
}

void Chip8Compiler::updateHash()
{
    char hex[SHA1_HEX_SIZE];
    char bpName[1024];
    sha1 sum;
    sum.add(code(), codeSize());
    for(uint32_t addr = 0; addr < OCTO_RAM_MAX; ++addr) {
        if(_impl->_program->breakpoints[addr]) {
            auto l = std::snprintf(bpName, 1023, "%04x:%s", addr, _impl->_program->breakpoints[addr]);
            sum.add(bpName, l);
        }
    }
    sum.finalize();
    sum.print_hex(hex);
    _impl->_sha1hex = hex;
}

void Chip8Compiler::updateLineCoverage()
{
    _impl->_lineCoverage.clear();
    _impl->_lineCoverage.resize(_impl->_program->source_line);
    if (!_impl->_program)
        return;
    for (size_t addr = 0; addr < OCTO_RAM_MAX; ++addr) {
        auto line = _impl->_program->romLineMap[addr];
        if (line < _impl->_lineCoverage.size()) {
            auto& range = _impl->_lineCoverage.at(line);
            if (range.first > addr || range.first == 0xffffffff)
                range.first = addr;
            if (range.second < addr || range.second == 0xffffffff)
                range.second = addr;
        }
    }
}

}
