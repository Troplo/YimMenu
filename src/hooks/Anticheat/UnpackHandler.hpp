#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace big
{
    struct UnpackContext
    {
        uint8_t* data;
        uint8_t* locations;
        const uint64_t* relocations;
        uint32_t key;
        uint32_t (*decryptionFunc)(uint32_t, uint32_t);
        uint64_t v2_val;
        uint64_t v14_val;
    };

    class UnpackHandler
    {
    public:
        struct TextSnapshot
        {
            uint64_t rva;
            std::vector<uint8_t> data;
        };

        struct ExportLocation
        {
            uint64_t rva;
            uint64_t size;
            std::vector<uint8_t> data;
        };

        struct Location
        {
            uint64_t rva;
            uint64_t size;
        };

        struct FileHeader
        {
            char magic[8];
            uint32_t version;
            uint32_t locationCount;
        };

        struct LocationHeader
        {
            uint64_t rva;
            uint64_t size;
        };

        struct TestAddrsHeader
        {
            char magic[8];
            uint32_t version;
            uint32_t locationCount;
        };

        struct TestAddr
        {
            uint64_t rva;
            uint64_t size;
        };

        static bool TakeTextSnapshot();
        static bool CompareTextSnapshot();
        static bool Export(const std::string& path);
        static bool Import(const std::string& path);
        static void DoExport();
        static void DoImport();
        static void DoUnpack();

        static std::vector<Location> GetExportLocations();

    private:
        static std::vector<TextSnapshot> m_textSnapshots;
        static std::vector<ExportLocation> m_locations;

        static bool DecodeLocation(const uint8_t** stream, uint64_t* offset, uint64_t* size);
        static void DecodeStream(const uint8_t** stream, uint32_t* offset, uint32_t* size);
        static bool WriteExportFile(const std::string& path, const std::vector<ExportLocation>& locations);
        static bool ReadExportFile(const std::string& path, std::vector<ExportLocation>& locations);
        static bool WriteTestAddresses(const std::string& path, const std::vector<ExportLocation>& locations);
        static bool ReadTestAddresses(const std::string& path, std::vector<TestAddr>& addresses);
        static void Unpack(UnpackContext& ctx);
    };
}