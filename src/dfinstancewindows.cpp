/*
Dwarf Therapist
Copyright (c) 2009 Trey Stout (chmod)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <QtGui>
#include <QtDebug>

#ifdef Q_WS_WIN
#include <windows.h>
#include <psapi.h>

#include "dfinstance.h"
#include "dfinstancewindows.h"
#include "defines.h"
#include "truncatingfilelogger.h"
#include "dwarf.h"
#include "utils.h"
#include "gamedatareader.h"
#include "memorylayout.h"
#include "cp437codec.h"
#include "win_structs.h"
#include "memorysegment.h"
#include "dwarftherapist.h"

DFInstanceWindows::DFInstanceWindows(QObject* parent)
    : DFInstance(parent)
    , m_proc(0)
{}

DFInstanceWindows::~DFInstanceWindows() {
    if (m_proc) {
        CloseHandle(m_proc);
    }
}

uint DFInstanceWindows::calculate_checksum() {
    char expect_M = read_char(m_base_addr);
    char expect_Z = read_char(m_base_addr + 0x1);

    if (expect_M != 'M' || expect_Z != 'Z') {
        qWarning() << "invalid executable";
    }
    uint pe_header = m_base_addr + read_int(m_base_addr + 30 * 2);
    char expect_P = read_char(pe_header);
    char expect_E = read_char(pe_header + 0x1);
    if (expect_P != 'P' || expect_E != 'E') {
        qWarning() << "PE header invalid";
    }

    uint timestamp = read_uint(pe_header + 4 + 2 * 2);
    QDateTime compile_timestamp = QDateTime::fromTime_t(timestamp);
    LOGD << "Target EXE was compiled at " <<
            compile_timestamp.toString(Qt::ISODate);
    return timestamp;
}

QVector<uint> DFInstanceWindows::enumerate_vector(const uint &addr) {
    TRACE << "beginning vector enumeration at" << hex << addr;
    QVector<uint> addresses;
    uint start = read_uint(addr + 4);
    TRACE << "start of vector" << hex << start;
    uint end = read_uint(addr + 8);
    TRACE << "end of vector" << hex << end;

    uint entries = (end - start) / sizeof(uint);
    TRACE << "there appears to be" << entries << "entries in this vector";

    if (m_layout->is_complete()) {
        Q_ASSERT(start >= 0);
        Q_ASSERT(end >= 0);
        Q_ASSERT(end >= start);
        Q_ASSERT((end - start) % 4 == 0);
        Q_ASSERT(start % 4 == 0);
        Q_ASSERT(end % 4 == 0);
        Q_ASSERT(entries < 5000);
    }

    for (uint ptr = start; ptr < end; ptr += 4 ) {
        uint a = read_uint(ptr);
        //if (is_valid_address(a)) {
            addresses.append(a);
        //}
    }
    TRACE << "FOUND" << addresses.size()<< "addresses in vector at"
            << hexify(addr);
    return addresses;
}

QString DFInstanceWindows::read_string(const uint &addr) {
    int len = read_int(addr + STRING_LENGTH_OFFSET);
    int cap = read_int(addr + STRING_CAP_OFFSET);
    uint buffer_addr = addr + STRING_BUFFER_OFFSET;
    if (cap >= 16)
        buffer_addr = read_uint(buffer_addr);

    if (len > cap || len < 0 || len > 1024) {
#ifdef _DEBUG
        // probaby not really a string
        LOGW << "Tried to read a string at" << hex << addr
            << "but it was totally not a string...";
#endif
        return QString();
    }
    Q_ASSERT_X(len <= cap, "read_string",
               "Length must be less than or equal to capacity!");
    Q_ASSERT_X(len >= 0, "read_string", "Length must be >=0!");
    Q_ASSERT_X(len < (1 << 16), "read_string",
               "String must be of sane length!");

    char *buffer = new char[len];
    read_raw(buffer_addr, len, buffer);

    CP437Codec *codec = new CP437Codec;
    QString ret_val = codec->toUnicode(buffer, len);
    delete[] buffer;
    //delete codec; seems to cause Qt Warnings if you delete this.
    return ret_val;
}

uint DFInstanceWindows::write_string(const uint &addr, const QString &str) {
    int cap = read_int(addr + STRING_CAP_OFFSET);
    uint buffer_addr = addr + STRING_BUFFER_OFFSET;
    if( cap >= 16 )
        buffer_addr = read_uint(buffer_addr);

    int len = qMin<int>(str.length(), cap);
    write_int(addr + STRING_LENGTH_OFFSET, len);

    CP437Codec *codec = new CP437Codec;
    QByteArray data = codec->fromUnicode(str);
    uint bytes_written = write_raw(buffer_addr, len, data.data());
    //delete codec; seems to cause Qt Warnings if you delete this.
    return bytes_written;
}

short DFInstanceWindows::read_short(const uint &addr) {
    char cval[2];
    memset(cval, 0, 2);
    ReadProcessMemory(m_proc, (LPCVOID)addr, &cval, 2, 0);
    return static_cast<short>((int)cval[0] + (int)(cval[1] >> 8));
}

ushort DFInstanceWindows::read_ushort(const uint &addr) {
    ushort val = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, &val, sizeof(ushort), 0);
    return val;
}

int DFInstanceWindows::read_int(const uint &addr) {
    int val = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, &val, sizeof(int), 0);
    return val;
}

uint DFInstanceWindows::read_uint(const uint &addr) {
    uint val = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, &val, sizeof(uint), 0);
    return val;
}

uint DFInstanceWindows::write_int(const uint &addr, const int &val) {
    uint bytes_written = 0;
    WriteProcessMemory(m_proc, (LPVOID)addr, &val, sizeof(int),
                       (DWORD*)&bytes_written);
    return bytes_written;
}

char DFInstanceWindows::read_char(const uint &addr) {
    char val = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, &val, sizeof(char), 0);
    return val;
}

int DFInstanceWindows::read_raw(const uint &addr, const uint &bytes,
                                QByteArray &buffer) {
    buffer.fill(0, bytes);
    int bytes_read = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, (char*)buffer.data(),
                      sizeof(BYTE) * bytes, (DWORD*)&bytes_read);
    return bytes_read;
}

uint DFInstanceWindows::read_raw(const uint &addr, const uint &bytes,
                                 void *buffer) {
    memset(buffer, 0, bytes);
    uint bytes_read = 0;
    ReadProcessMemory(m_proc, (LPCVOID)addr, (void*)buffer,
                      sizeof(uchar) * bytes, (DWORD*)&bytes_read);
#ifdef _DEBUG
    if (bytes_read != bytes) {
        LOGW << "tried to get" << bytes << "bytes from" << hex << addr
            << "but only got" << dec << bytes_read << "Windows System Error("
            << dec << GetLastError() << ")";
    }
#endif
    return bytes_read;
}

uint DFInstanceWindows::write_raw(const uint &addr, const uint &bytes,
                                  void *buffer) {
    uint bytes_written = 0;
    WriteProcessMemory(m_proc, (LPVOID)addr, (void*)buffer,
                       sizeof(uchar) * bytes, (DWORD*)&bytes_written);
    Q_ASSERT(bytes_written == bytes);
    return bytes_written;
}

bool DFInstanceWindows::find_running_copy() {
    LOGD << "attempting to find running copy of DF by window handle";
    m_is_ok = false;

    HWND hwnd = FindWindow(L"OpenGL", L"Dwarf Fortress");
    if (!hwnd)
        hwnd = FindWindow(L"SDL_app", L"Dwarf Fortress");
    if (!hwnd)
        hwnd = FindWindow(NULL, L"Dwarf Fortress");

    if (!hwnd) {
        QMessageBox::warning(0, tr("Warning"),
            tr("Unable to locate a running copy of Dwarf "
            "Fortress, are you sure it's running?"));
        LOGW << "can't find running copy";
        return m_is_ok;
    }
    LOGD << "found copy with HWND: " << hwnd;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return m_is_ok;
    }
    LOGD << "PID of process is: " << pid;
    m_pid = pid;
    m_hwnd = hwnd;

    m_proc = OpenProcess(PROCESS_QUERY_INFORMATION
                         | PROCESS_VM_READ
                         | PROCESS_VM_OPERATION
                         | PROCESS_VM_WRITE, false, m_pid);
    LOGD << "PROC HANDLE:" << m_proc;
    if (m_proc == NULL) {
        LOGE << "Error opening process!" << GetLastError();
    }

    PVOID peb_addr = GetPebAddress(m_proc);
    LOGD << "PEB is at: " << hex << peb_addr;

    QString connection_error = tr("I'm sorry. I'm having trouble connecting to "
                                  "DF. I can't seem to locate the PEB address "
                                  "of the process. \n\nPlease re-launch DF and "
                                  "try again.");
    if (peb_addr == 0){
        QMessageBox::critical(0, tr("Connection Error"), connection_error);
        qCritical() << "PEB address came back as 0";
    } else {
        PEB peb;
        DWORD bytes = 0;
        if (ReadProcessMemory(m_proc, (PCHAR)peb_addr, &peb, sizeof(PEB), &bytes)) {
            LOGD << "read" << bytes << "bytes BASE ADDR is at: " << hex << peb.ImageBaseAddress;
            m_base_addr = (int)peb.ImageBaseAddress;
            m_is_ok = true;
        } else {
            QMessageBox::critical(0, tr("Connection Error"), connection_error);
            qCritical() << "unable to read remote PEB!" << GetLastError();
            m_is_ok = false;
        }
    }

    if (m_is_ok) {
        m_layout = get_memory_layout(hexify(calculate_checksum()).toLower());
    }

    if (!m_is_ok) // time to bail
        return m_is_ok;

    m_memory_correction = (int)m_base_addr - 0x0400000;
    LOGD << "base address:" << hexify(m_base_addr);
    LOGD << "memory correction:" << hexify(m_memory_correction);

    map_virtual_memory();

    if (DT->user_settings()->value("options/alert_on_lost_connection",
                                   true).toBool()) {
        m_heartbeat_timer->start(1000); // check every second for disconnection
    }
    m_is_ok = true;
    return m_is_ok;
}

/*! OS specific way of asking the kernel for valid virtual memory pages from
  the DF process. These pages are used to speed up scanning, and validate
  reads from DF's memory. If addresses are not within ranges found by this
  method, they will fail the is_valid_address() method */
void DFInstanceWindows::map_virtual_memory() {
    // destroy existing segments
    foreach(MemorySegment *seg, m_regions) {
        delete(seg);
    }
    m_regions.clear();

    if (!m_is_ok)
        return;

    // start by figuring out what kernel we're talking to
    TRACE << "Mapping out virtual memory";
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    TRACE << "PROCESSORS:" << info.dwNumberOfProcessors;
    TRACE << "PROC TYPE:" << info.wProcessorArchitecture <<
            info.wProcessorLevel <<
            info.wProcessorRevision;
    TRACE << "PAGE SIZE" << info.dwPageSize;
    TRACE << "MIN ADDRESS:" << hexify((uint)info.lpMinimumApplicationAddress);
    TRACE << "MAX ADDRESS:" << hexify((uint)info.lpMaximumApplicationAddress);

    uint start = (uint)info.lpMinimumApplicationAddress;
    uint max_address = (uint)info.lpMaximumApplicationAddress;
    int page_size = info.dwPageSize;
    int accepted = 0;
    int rejected = 0;
    uint segment_start = start;
    uint segment_size = page_size;
    while (start < max_address) {
        MEMORY_BASIC_INFORMATION mbi;
        int sz = VirtualQueryEx(m_proc, (void*)start, &mbi,
                                sizeof(MEMORY_BASIC_INFORMATION));
        if (sz != sizeof(MEMORY_BASIC_INFORMATION)) {
            // incomplete data returned. increment start and move on...
            start += page_size;
            continue;
        }

        segment_start = (uint)mbi.BaseAddress;
        segment_size = (uint)mbi.RegionSize;

        if (mbi.State == MEM_COMMIT
            //&& !(mbi.Protect & PAGE_GUARD)
            && (mbi.Protect & PAGE_EXECUTE_READ ||
                mbi.Protect & PAGE_EXECUTE_READWRITE ||
                mbi.Protect & PAGE_READONLY ||
                mbi.Protect & PAGE_READWRITE ||
                mbi.Protect & PAGE_WRITECOPY)) {
            TRACE << "FOUND READABLE COMMITED MEMORY SEGMENT FROM" <<
                    hexify(segment_start) << "-" <<
                    hexify(segment_start + segment_size) <<
                    "SIZE:" << (segment_size / 1024.0f) << "KB" <<
                    "FLAGS:" << mbi.Protect;
            MemorySegment *segment = new MemorySegment("", segment_start,
                                                       segment_start
                                                       + segment_size);
            segment->is_guarded = mbi.Protect & PAGE_GUARD;
            m_regions << segment;
            accepted++;
        } else {
            TRACE << "REJECTING MEMORY SEGMENT AT" << hexify(segment_start) <<
                    "SIZE:" << (segment_size / 1024.0f) << "KB FLAGS:" <<
                    mbi.Protect;
            rejected++;
        }
        if (mbi.RegionSize)
            start += mbi.RegionSize;
        else
            start += page_size;
    }
    m_lowest_address = 0xFFFFFFFF;
    m_highest_address = 0;
    foreach(MemorySegment *seg, m_regions) {
        if (seg->start_addr < m_lowest_address)
            m_lowest_address = seg->start_addr;
        if (seg->end_addr > m_highest_address)
            m_highest_address = seg->end_addr;
    }
    TRACE << "MEMORY SEGMENT SUMMARY: accepted" << accepted << "rejected" <<
            rejected << "total" << accepted + rejected;
}
#endif
