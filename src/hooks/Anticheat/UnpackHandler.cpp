#include "UnpackHandler.hpp"
#include "util/current_module.hpp"
#include "pointers.hpp"

#include <windows.h>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>

#define MERGE_PACKS 0

namespace big
{
    extern std::mutex g_unpackLocationsMutex;
    extern std::vector<std::pair<uintptr_t, uint32_t>> g_unpackLocations;

    std::vector<UnpackHandler::TextSnapshot> UnpackHandler::m_textSnapshots{};
    std::vector<UnpackHandler::ExportLocation> UnpackHandler::m_locations{};

    static void DumpBytes(const char* label, const uint8_t* data, uint64_t size)
    {
        LOG(VERBOSE) << label << " size=0x" << std::hex << size;

        for (uint64_t i = 0; i < size; ++i)
        {
            LOG(VERBOSE) << "   +0x" << std::hex << i << " = 0x" << static_cast<uint32_t>(data[i]);
        }
    }

    namespace
    {
        constexpr char TESTADDRS_MAGIC[9] = {'T', 'E', 'S', 'T', 'A', 'D', 'D', 'R', '\0'};
        constexpr uint32_t TESTADDRS_VERSION = 1;

        constexpr char PARAPAK_MAGIC[8] = {'P', 'A', 'R', 'A', 'P', 'A', 'K', '\0'};
        constexpr uint32_t PARAPAK_VERSION = 1;

        uint8_t* GetModuleBase()
        {
            const auto moduleName = GetCurrentModule();
            return reinterpret_cast<uint8_t*>(GetModuleHandleA(moduleName.empty() ? nullptr : moduleName.c_str()));
        }

        uint64_t GetImageSize()
        {
            uint8_t* mod = GetModuleBase();
            if (!mod) return 0;

            auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
            if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;

            auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(mod + dosHeader->e_lfanew);
            if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;

            return ntHeaders->OptionalHeader.SizeOfImage;
        }

        std::vector<UnpackHandler::Location> GetTextSections(uint8_t* imageBase)
        {
            std::vector<UnpackHandler::Location> sections;
            if (!imageBase) return sections;

            auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(imageBase);
            if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return sections;

            auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(imageBase + dosHeader->e_lfanew);
            if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return sections;

            IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);

            for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i)
            {
                char name[9]{};
                std::memcpy(name, section[i].Name, sizeof(section[i].Name));

                LOG(VERBOSE)
                    << "Section "
                    << name
                    << " RVA 0x"
                    << std::hex << section[i].VirtualAddress
                    << " VirtualSize 0x"
                    << section[i].Misc.VirtualSize
                    << " RawSize 0x"
                    << section[i].SizeOfRawData
                    << " end RVA 0x"
                    << (static_cast<uint64_t>(section[i].VirtualAddress) +
                        section[i].Misc.VirtualSize);

                if (std::strcmp(name, ".data") != 0 && std::strcmp(name, ".tls") != 0 && section[i].Misc.VirtualSize != 0)
                {
                    sections.emplace_back(section[i].VirtualAddress, section[i].Misc.VirtualSize);
                }
            }
            return sections;
        }

        bool IsInTextSections(uint64_t rva, uint64_t size, const std::vector<UnpackHandler::Location>& textSections)
        {
            for (const auto& section : textSections)
            {
                if (rva >= section.rva)
                {
                    const uint64_t offset = rva - section.rva;
                    if (offset <= section.size && size <= section.size - offset)
                        return true;
                }
            }
            return false;
        }
    }

    std::vector<UnpackHandler::ExportLocation> GetMemcpyLocations()
    {
        std::vector<UnpackHandler::ExportLocation> locations;

        std::lock_guard<std::mutex> lock(g_unpackLocationsMutex);

        for (const auto& [dest, size] : g_unpackLocations)
        {
            const uintptr_t imageBase =
                reinterpret_cast<uintptr_t>(GetModuleBase());

            if (dest < imageBase)
                continue;

            const uint64_t rva =
                static_cast<uint64_t>(dest - imageBase);

            if (size == 0)
                continue;

            locations.push_back({
                rva,
                size,
                {}
            });
        }

        return locations;
    }

    bool UnpackHandler::DecodeLocation(const uint8_t** stream, uint64_t* offset, uint64_t* size)
    {
        uint64_t delta = 0, shift = 0;
        while (true)
        {
            const uint8_t b = *(*stream)++;
            delta |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
        }
        *offset += delta + *size;
        if (*offset == 0xFFFFFFFFFFFFFFFF) return false;

        uint64_t newSize = 0;
        shift = 0;
        while (true)
        {
            const uint8_t b = *(*stream)++;
            newSize |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
        }
        *size = newSize;
        return true;
    }

    void UnpackHandler::DecodeStream(const uint8_t** stream, uint32_t* offset, uint32_t* size)
    {
        uint32_t delta = 0, shift = 0;
        while (true)
        {
            uint8_t b = *(*stream)++;
            delta |= (b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80)) break;
        }
        *offset += delta + *size;
        if (*offset == 0xFFFFFFFF) return;

        uint32_t newSize = 0;
        shift = 0;
        while (true)
        {
            uint8_t b = *(*stream)++;
            newSize |= (b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80)) break;
        }
        *size = newSize;
    }

    std::vector<UnpackHandler::Location> UnpackHandler::GetExportLocations()
    {
        static const std::vector<uint64_t> locationStreams = { 0x03C4C8F9, 0x0359C024, 0x03188000 };
        std::vector<Location> locations;

        for (const uint64_t streamRva : locationStreams)
        {
            const uint8_t* stream = GetModuleBase() + streamRva;
            uint64_t offset = 0, size = 0;

            while (DecodeLocation(&stream, &offset, &size))
            {
                LOG(VERBOSE) << "Location " << std::hex << offset << " size: " << size << " block: " << std::hex << streamRva;
                locations.push_back({ offset, size });
            }
        }
        return locations;
    }

    bool UnpackHandler::WriteExportFile(const std::string& path, const std::vector<ExportLocation>& locations)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) return false;

        FileHeader header{};
        std::memcpy(header.magic, PARAPAK_MAGIC, sizeof(header.magic));
        header.version = PARAPAK_VERSION;
        header.locationCount = static_cast<uint32_t>(locations.size());

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!file) return false;

        for (const auto& location : locations)
        {
            LocationHeader locationHeader{};
            locationHeader.rva = location.rva;
            locationHeader.size = location.size;

            file.write(reinterpret_cast<const char*>(&locationHeader), sizeof(locationHeader));
            if (location.size != 0)
                file.write(reinterpret_cast<const char*>(location.data.data()), location.size);
        }
        return true;
    }

    bool UnpackHandler::ReadExportFile(const std::string& path, std::vector<ExportLocation>& locations)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;

        FileHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (std::memcmp(header.magic, PARAPAK_MAGIC, sizeof(header.magic)) != 0 || header.version != PARAPAK_VERSION)
            return false;

        locations.clear();
        locations.reserve(header.locationCount);

        for (uint32_t i = 0; i < header.locationCount; ++i)
        {
            LocationHeader locationHeader{};
            file.read(reinterpret_cast<char*>(&locationHeader), sizeof(locationHeader));

            ExportLocation location{};
            location.rva = locationHeader.rva;
            location.size = locationHeader.size;
            location.data.resize(location.size);

            if (location.size != 0)
                file.read(reinterpret_cast<char*>(location.data.data()), location.size);

            locations.push_back(std::move(location));
        }
        return true;
    }

    bool UnpackHandler::TakeTextSnapshot()
    {
        uint8_t* imageBase = GetModuleBase();
        if (!imageBase) return false;

        auto textSections = GetTextSections(imageBase);
        if (textSections.empty()) return false;

        m_textSnapshots.clear();
        m_textSnapshots.reserve(textSections.size());

        for (const auto& section : textSections)
        {
            TextSnapshot snapshot{};
            snapshot.rva = section.rva;
            snapshot.data.resize(section.size);
            std::memcpy(snapshot.data.data(), imageBase + section.rva, section.size);
            m_textSnapshots.push_back(std::move(snapshot));

            LOG(VERBOSE) << "Captured .text snapshot: RVA 0x" << std::hex << section.rva << " size 0x" << section.size;
        }
        return true;
    }

    bool UnpackHandler::CompareTextSnapshot()
    {
        if (m_textSnapshots.empty())
        {
            LOG(VERBOSE) << "No .text snapshots have been taken";
            return false;
        }

        uint8_t* imageBase = GetModuleBase();
        if (!imageBase) return false;

        auto textSections = GetTextSections(imageBase);

        std::vector<std::pair<uintptr_t, uint32_t>> vehBlocks;
        {
            LOG(VERBOSE) << "The number of .text sections changed: old=" << std::dec << m_textSnapshots.size() << " new=" << textSections.size();
            std::lock_guard<std::mutex> lock(g_unpackLocationsMutex);
            vehBlocks = g_unpackLocations;
        }

        LOG(VERBOSE) << "Retrieved " << std::dec << vehBlocks.size() << " block(s) from VEH memcpy intercepts.";

        m_locations.clear();
        uint64_t totalDifferenceCount = 0;
        uint64_t filteredDifferenceCount = 0;

        for (size_t s = 0; s < textSections.size(); ++s)
        {
            const auto& currentSection = textSections[s];
            const auto& snapshot = m_textSnapshots[s];

            if (currentSection.rva != snapshot.rva || currentSection.size != snapshot.data.size())
                return false;

            const uint8_t* currentData = imageBase + currentSection.rva;

            for (uint64_t i = 0; i < currentSection.size; ++i)
            {
                const uint8_t oldByte = snapshot.data[i];
                const uint8_t newByte = currentData[i];

                if (oldByte == newByte) continue;

                ++totalDifferenceCount;
                const uint64_t rva = currentSection.rva + i;
                const uintptr_t absAddress = reinterpret_cast<uintptr_t>(imageBase) + rva;

                bool wasCopiedByMemcpy = false;
                for (const auto& [dest, size] : vehBlocks)
                {
                    if (absAddress >= dest && absAddress < (dest + size))
                    {
                        wasCopiedByMemcpy = true;
                        break;
                    }
                }
                LOG(VERBOSE) << ".text changed at RVA 0x" << std::hex << rva << " offset 0x" << i << ": 0x" << static_cast<uint32_t>(oldByte) << " -> 0x" << static_cast<uint32_t>(newByte) << "SECT: " << currentSection.rva << " MEMCPY: " << wasCopiedByMemcpy;

                if (wasCopiedByMemcpy || currentSection.rva == 0x19ce600 || 1)
                {
                    if (!m_locations.empty() && m_locations.back().rva + m_locations.back().size == rva)
                    {
                        m_locations.back().data.push_back(newByte);
                        m_locations.back().size++;
                    }
                    else
                    {
                        ExportLocation location{};
                        location.rva = rva;
                        location.size = 1;
                        location.data.push_back(newByte);
                        m_locations.push_back(std::move(location));
                    }
                    ++filteredDifferenceCount;
                }
            }
        }
        LOG(VERBOSE) << ".text comparison complete: " << std::dec << totalDifferenceCount << " total byte(s) changed.";
        LOG(VERBOSE) << "Filtered to " << filteredDifferenceCount << " byte(s) touched by memcpy.";

        auto memcpySnaps = GetMemcpyLocations();

        for (const auto& snap : memcpySnaps)
        {
            bool found = false;

            for (const auto& location : m_locations)
            {
                const uint64_t memcpyStart = snap.rva;
                const uint64_t memcpyEnd = snap.rva + snap.size;

                const uint64_t locationStart = location.rva;
                const uint64_t locationEnd = location.rva + location.size;

                if (memcpyStart < locationEnd && locationStart < memcpyEnd)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                LOG(VERBOSE)
                    << "Memcpy destination unchanged somehow?: "
                    << "RVA 0x" << std::hex << snap.rva
                    << " size 0x" << snap.size;
            }
        }
        return true;
    }

    bool UnpackHandler::Export(const std::string& path)
    {
        uint8_t* imageBase = GetModuleBase();
        if (!imageBase) return false;

        std::vector<ExportLocation> locations;
        locations.reserve(m_locations.size() + 100);

        for (const auto& location : m_locations)
        {
            ExportLocation exported{};
            exported.rva = location.rva;
            exported.size = location.size;
            exported.data.resize(location.size);

            DumpBytes("EXPORT", exported.data.data(), exported.size);

            std::memcpy(exported.data.data(), imageBase + location.rva, location.size);
            LOG(VERBOSE) << "Exporting RVA 0x" << std::hex << location.rva << " size 0x" << location.size;
            locations.push_back(std::move(exported));
        }

        const uint8_t* packerStream = g_pointers->m_gta.PackerList1;
        if (packerStream)
        {
            uint32_t blockOffset = 0;
            uint32_t blockSize = 0;

            while (true)
            {
                DecodeStream(&packerStream, &blockOffset, &blockSize);

                if (blockOffset == 0xFFFFFFFF)
                    break;

                ExportLocation exported{};
                exported.rva = blockOffset;
                exported.size = blockSize;
                exported.data.resize(blockSize);

                std::memcpy(exported.data.data(), imageBase + blockOffset, blockSize);

                DumpBytes("EXPORT_PACKER_LIST", exported.data.data(), exported.size);
                locations.push_back(std::move(exported));

                LOG(VERBOSE) << "Exporting PackerList1 location RVA 0x" << std::hex << blockOffset << " size 0x" << blockSize;
            }
        }

        if (!WriteExportFile(path, locations))
            return false;

        LOG(VERBOSE) << "Exported " << std::dec << locations.size() << " locations to " << path;
        return true;
    }

    bool UnpackHandler::Import(const std::string& path)
    {
        uint8_t* imageBase = GetModuleBase();
        if (!imageBase) return false;

        std::vector<UnpackHandler::ExportLocation> locations{};
        if (!ReadExportFile(path, locations)) return false;

        const auto imageSize = GetImageSize();

        for (const auto& location : locations)
        {
            if (location.rva >= imageSize || location.size > imageSize - location.rva) return false;
            std::memcpy(imageBase + location.rva, location.data.data(), location.size);

            DumpBytes("IMPORT DEST AFTER", imageBase + location.rva, location.size);
            LOG(VERBOSE) << "Imported RVA 0x" << std::hex << location.rva << " size 0x" << location.size;
        }

        LOG(VERBOSE) << "Imported " << locations.size() << " locations from " << path;
        return true;
    }

    void UnpackHandler::DoExport()
    {
    	auto file = g_file_manager.get_baked_project_file(std::filesystem::path("ParaPaks") / "legacy_3889.parapak");
        Export(file.get_path().string());
    }

    void UnpackHandler::DoImport()
    {
    	auto file = g_file_manager.get_baked_project_file(std::filesystem::path("ParaPaks") / "legacy_3889.parapak");
        Import(file.get_path().string());
    }

    bool UnpackHandler::WriteTestAddresses(const std::string& path, const std::vector<ExportLocation>& locations)
    {
        uint8_t* imageBase = GetModuleBase();
        if (!imageBase)
            return false;

        auto textSections = GetTextSections(imageBase);
        if (textSections.empty())
        {
            LOG(VERBOSE) << "Failed to find any .text sections";
            return false;
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;

        TestAddrsHeader header{};
        std::memcpy(header.magic, TESTADDRS_MAGIC, sizeof(header.magic));
        header.version = TESTADDRS_VERSION;
        header.locationCount = static_cast<uint32_t>(locations.size());

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!file)
            return false;

        for (const auto& location : locations)
        {
            if (!IsInTextSections(location.rva, location.size, textSections))
            {
                LOG(VERBOSE) << "Skipping non-.text address: RVA 0x" << std::hex << location.rva << " size 0x" << location.size;
                continue;
            }

            TestAddr address{};
            address.rva = location.rva;
            address.size = location.size;

            file.write(reinterpret_cast<const char*>(&address), sizeof(address));
            if (!file)
                return false;
        }

        return true;
    }

    bool UnpackHandler::ReadTestAddresses(const std::string& path, std::vector<TestAddr>& addresses)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;

        TestAddrsHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file)
            return false;

        if (std::memcmp(header.magic, TESTADDRS_MAGIC, sizeof(header.magic)) != 0)
            return false;

        if (header.version != TESTADDRS_VERSION)
            return false;

        addresses.clear();
        addresses.reserve(header.locationCount);

        for (uint32_t i = 0; i < header.locationCount; ++i)
        {
            TestAddr address{};
            file.read(reinterpret_cast<char*>(&address), sizeof(address));
            if (!file)
                return false;

            addresses.push_back(address);
        }

        return true;
    }

    void UnpackHandler::Unpack(UnpackContext& ctx)
    {
        uint8_t* imageBase = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));

        uint32_t bytesToCopy = 0;
        uint32_t blockOffset = 0;
        uint32_t blockSize = 0;
        uint32_t relocOffset = 0xFFFFFFFF;

        const uint32_t* relocStream = reinterpret_cast<const uint32_t*>(ctx.relocations);

        uint32_t hasPendingReloc = 0;
        uint32_t relocBytesRemaining = 0;

        uint8_t decryptedBuffer[4] = {0};
        uint32_t decryptedBufferOffset = 4;
        uint8_t relocBuffer[8] = {0};

        LOG(VERBOSE) << "Starting unpack, module base: " << reinterpret_cast<void*>(imageBase);

        while (true)
        {
            const uint8_t* streamPtr = ctx.locations;
            DecodeStream(&streamPtr, &blockOffset, &blockSize);
            ctx.locations = const_cast<uint8_t*>(streamPtr);

            if (blockOffset == 0xFFFFFFFF)
            {
                LOG(VERBOSE) << "End reached, blockOffset: -1";
                break;
            }

            uint8_t* outputPtr = imageBase + blockOffset;
            uint8_t* blockEnd = outputPtr + blockSize;
            LOG(VERBOSE) << "Processing block at " << reinterpret_cast<void*>(outputPtr) << " size " << blockSize;

            while (outputPtr != blockEnd)
            {
                if (decryptedBufferOffset == 4)
                {
                    uint32_t encryptedChunk = 0;
                    std::memcpy(&encryptedChunk, ctx.data, 4);

                    uint32_t decryptedChunk = ctx.decryptionFunc(encryptedChunk, ctx.key);
                    std::memcpy(decryptedBuffer, &decryptedChunk, 4);

                    ctx.data += 4;
                    decryptedBufferOffset = 0;
                }

                bytesToCopy = static_cast<uint32_t>(blockEnd - outputPtr);

                if ((4 - decryptedBufferOffset) < bytesToCopy)
                {
                    bytesToCopy = 4 - decryptedBufferOffset;
                }

                if (relocBytesRemaining == 0 && ctx.v14_val != 0)
                {
                    if (hasPendingReloc == 0)
                    {
                        if (relocStream)
                        {
                            relocOffset = relocStream[0];
                            relocStream += 2;
                            hasPendingReloc = 1;
                        }
                    }

                    if (relocOffset != 0xFFFFFFFF)
                    {
                        if (imageBase + relocOffset == outputPtr)
                        {
                            relocBytesRemaining = 8;
                        }
                        else if (static_cast<uint32_t>((imageBase + relocOffset) - outputPtr) < bytesToCopy)
                        {
                            bytesToCopy = static_cast<uint32_t>((imageBase + relocOffset) - outputPtr);
                        }
                    }
                }

                if (relocBytesRemaining != 0)
                {
                    if (relocBytesRemaining < bytesToCopy)
                        bytesToCopy = relocBytesRemaining;
                    std::memcpy(&relocBuffer[8 - relocBytesRemaining], &decryptedBuffer[decryptedBufferOffset], bytesToCopy);
                    relocBytesRemaining -= bytesToCopy;
                    if (relocBytesRemaining == 0)
                    {
                        uint64_t relocatedValue = ctx.v2_val + ctx.v14_val;
                        std::memcpy(outputPtr, &relocatedValue, 8);

                        hasPendingReloc = 0;
                        outputPtr += 8;
                    }
                }
                else
                {
                    std::memcpy(outputPtr, &decryptedBuffer[decryptedBufferOffset], bytesToCopy);
                    outputPtr += bytesToCopy;
                }

                decryptedBufferOffset += bytesToCopy;
            }
        }
    }

    void UnpackHandler::DoUnpack()
    {
        // UnpackContext ctx = {};
        // ctx.data = reinterpret_cast<uint8_t*>(Pointers.PackerList1);
        // ctx.locations = reinterpret_cast<uint8_t*>(Pointers.Packer1ListLocations);
        // ctx.relocations = reinterpret_cast<const uint64_t*>(0x0FFFFFFFFFFFFFFFF);

        // ctx.key = 0x4CF737F8;
        // ctx.decryptionFunc = [](uint32_t enc, uint32_t key) -> uint32_t {
            // return enc ^ key;
        // };

        // Unpack(ctx);
    }
}