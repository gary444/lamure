#ifndef LAMURE_DATA_H
#define LAMURE_DATA_H

#include "lamure/pro/common.h"

namespace prov
{
class Readable
{
  public:
    static const uint32_t HEADER_LENGTH = 6;

  protected:
    static bool read_header(ifstream &is)
    {
        uint16_t magic_bytes;
        //        uint32_t crc_32;
        uint32_t data_length;
        is.read(reinterpret_cast<char *>(&magic_bytes), 2);
        magic_bytes = swap(magic_bytes, true);
        if(magic_bytes != 0xAFFE)
        {
            std::stringstream sstr;
            sstr << "0x" << std::uppercase << std::hex << magic_bytes;
            throw std::runtime_error("File format is incompatible: magic bytes " + sstr.str() + " not equal 0xAFFE");
        }
        //        is.read(reinterpret_cast<char *>(&crc_32), 4);
        is.read(reinterpret_cast<char *>(&data_length), 4);
        //        crc_32 = swap(crc_32, true);
        data_length = swap(data_length, true);

        std::streampos fsize = is.tellg();
        is.seekg(0, std::ios::end);
        fsize = is.tellg() - fsize;

        if(fsize != data_length)
        {
            std::stringstream istr;
            istr << fsize;
            std::stringstream dstr;
            dstr << data_length;
            throw std::out_of_range("Readable length not equal to declared: " + istr.str() + " instead of " + dstr.str());
        }

        is.clear();
        is.seekg(6);

        //        std::vector<uint8_t> byte_buffer(data_length, 0);
        //        is.read(reinterpret_cast<char *>(&byte_buffer[0]), data_length);

        //        is.clear();
        //        is.seekg(10);

        //        boost::crc_32_type crc;
        //        crc.process_bytes(byte_buffer.data(), data_length);
        //        if(crc.checksum() != crc_32)
        //        {
        //            std::stringstream cstr;
        //            cstr << "0x" << std::uppercase << std::hex << crc.checksum();
        //            std::stringstream rstr;
        //            rstr << "0x" << std::uppercase << std::hex << crc_32;
        //            throw std::runtime_error("File is corrupted, crc32 checksums do not match: " + cstr.str() + " instead of " + rstr.str());
        //        }

        return true;
    }
};
}

#endif // LAMURE_DATA_H
