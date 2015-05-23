#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>
#include <iomanip>
#include <cstdlib>

class StreamInfo {
public:
    StreamInfo(uint64_t os, int ot, uint64_t sl, uint64_t il) {
        offset = os;
        offsetType = ot;
        streamLength = sl;
        inflatedLength = il;
        clevel = 9;
        window = 15;
        memlevel = 9;
        identBytes = 0;
        streamData = 0;
    }
    ~StreamInfo() {
        diffByteOffsets.clear();
        diffByteOffsets.shrink_to_fit();
        diffByteVal.clear();
        diffByteVal.shrink_to_fit();
    }
    uint64_t offset;
    int offsetType;
    uint64_t streamLength;
    uint64_t inflatedLength;
    uint8_t clevel;
    uint8_t window;
    uint8_t memlevel;
    uint64_t identBytes;
    std::vector<uint64_t> diffByteOffsets; // Offsets of bytes that differ, this is an incremental offset list
    std::vector<unsigned char> diffByteVal;
    unsigned char* streamData;
};

class ZlibWrapper {
public:
    ZlibWrapper(unsigned char *ni, uint64_t ai, unsigned char *no, uint64_t ao) {
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.next_in = ni;
        strm.avail_in = ai;
        next_out = no;
        avail_out = ao;
    }

    // Do decompression
    bool doInflate(bool hasHeader) {
        strm.next_in = strm.next_in; 
        strm.avail_in = strm.avail_in;
        if (hasHeader) {
            if (inflateInit(&strm) != Z_OK) return false;
        } else {
            if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
        }
        int ret = Z_BUF_ERROR;
        while (ret == Z_BUF_ERROR) { // If buffer too small use it again
            strm.next_out = next_out;
            strm.avail_out = avail_out;
            ret = inflate(&strm, Z_FINISH);
        }
        if (inflateEnd(&strm) != Z_OK) return false;
        return ret == Z_STREAM_END;
    }

    // Do compression
    bool doDeflate(int clevel, int window, int memlevel) {
        if (deflateInit2(&strm, clevel, Z_DEFLATED, window == 0 ? -MAX_WBITS : window, memlevel, Z_DEFAULT_STRATEGY) != Z_OK) return false;
        strm.next_out=next_out;
        strm.avail_out=avail_out;
        int ret = deflate(&strm, Z_FINISH);
        if (deflateEnd(&strm) != Z_OK) return false;
        return ret == Z_STREAM_END;
    }
    z_stream strm;
    unsigned char *next_out;
    uint64_t avail_out;
};

// A zlib stream has the following structure: (http://tools.ietf.org/html/rfc1950)
//  +---+---+   CMF: bits 0 to 3  CM      Compression method (8 = deflate)
//  |CMF|FLG|        bits 4 to 7  CINFO   Compression info (base-2 logarithm of the LZ77 window size minus 8)
//  +---+---+
//              FLG: bits 0 to 4  FCHECK  Check bits for CMF and FLG (in MSB order (CMF*256 + FLG) is a multiple of 31)
//                   bit  5       FDICT   Preset dictionary
//                   bits 6 to 7  FLEVEL  Compression level (0 = fastest, 1 = fast, 2 = default, 3 = maximum)
int parseOffsetType(int header) {
    switch (header) {
        case 0x2815 : return 0;  case 0x2853 : return 1;  case 0x2891 : return 2;  case 0x28cf : return 3;
        case 0x3811 : return 4;  case 0x384f : return 5;  case 0x388d : return 6;  case 0x38cb : return 7;
        case 0x480d : return 8;  case 0x484b : return 9;  case 0x4889 : return 10; case 0x48c7 : return 11;
        case 0x5809 : return 12; case 0x5847 : return 13; case 0x5885 : return 14; case 0x58c3 : return 15;
        case 0x6805 : return 16; case 0x6843 : return 17; case 0x6881 : return 18; case 0x68de : return 19;
        case 0x7801 : return 20; case 0x785e : return 21; case 0x789c : return 22; case 0x78da : return 23;
    }
    return -1;
}

bool checkOffset(unsigned char *next_in, uint64_t avail_in, uint64_t &total_in, uint64_t &total_out, bool hasHeader) {
    bool success = false;
    unsigned char* dBuffer = new unsigned char[1 << 16];
    ZlibWrapper zw(next_in, avail_in, dBuffer, 1 << 16);
    if (zw.doInflate(hasHeader) && zw.strm.total_in >= 16 && zw.strm.total_out > 0) {
        total_in = zw.strm.total_in;
        total_out = zw.strm.total_out;
        success = true;
    }
    delete [] dBuffer;
    return success;
}

bool testParameters(unsigned char *buffer, unsigned char *dBuffer, StreamInfo &streamInfo,
                    int window, int memlevel, int clevel)  {
    const int recompTreshold = 128; // Recompressed if differs in <= recompTreshold bytes
    const int sizeDiffTreshold = 128; // Compared when the size difference is <= sizeDiffTreshold
    const int testBlock = 1024; // Data are compressed to testBlock bytes first before full compression

    // Use default settings except window, clevel and memlevel
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = dBuffer;
    int ret = deflateInit2(&strm, clevel, Z_DEFLATED, window == 0 ? -MAX_WBITS : window, memlevel, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        std::cout << std::endl << "deflateInit() failed with exit code:" << ret << std::endl; // Should never happen
        abort();
    }
    const int recompSize = deflateBound(&strm, streamInfo.inflatedLength); // Allocate buffer for worst case
    unsigned char* rBuffer = new unsigned char[recompSize];
    strm.avail_out = std::min(recompSize, testBlock);
    strm.avail_in = streamInfo.inflatedLength;
    strm.next_out = rBuffer;
    ret = deflate(&strm, Z_FINISH); // Do the partial compression (first testBlock bytes)
    bool skip = false;
    if (ret == Z_OK && recompSize > testBlock) {
        // Check if full recompression is needed
        if (strm.total_out == testBlock && streamInfo.streamLength >= testBlock) {
            int identicalBytes = 0;
            for (int i = 0; i < testBlock ; i++) {
                if (rBuffer[i] == buffer[i + streamInfo.offset]) identicalBytes++;
            }
            skip = identicalBytes < testBlock - recompTreshold;
        }
        if (!skip) {
            strm.avail_out = recompSize - testBlock;
            ret = deflate(&strm, Z_FINISH); // Do full recompression
        }
    }
    if (ret != Z_STREAM_END && (!skip || ret != Z_OK)) {
        std::cout << std::endl << "recompression failed with exit code:" << ret << std::endl;
        abort();
    }

    bool fullmatch = false;
    skip |= abs((strm.total_out - streamInfo.streamLength)) > sizeDiffTreshold;
    if (!skip) {
        uint64_t i, smaller = strm.total_out < streamInfo.streamLength ? strm.total_out : streamInfo.streamLength;
        int_fast64_t identicalBytes = 0;
        for (i = 0; i < smaller; i++) {
            if (rBuffer[i] == buffer[i + streamInfo.offset]) identicalBytes++;
        }
        if (streamInfo.streamLength - identicalBytes <= recompTreshold && identicalBytes > streamInfo.identBytes) {
            streamInfo.identBytes = identicalBytes;
            streamInfo.clevel = clevel;
            streamInfo.memlevel = memlevel;
            streamInfo.window = window;
            streamInfo.diffByteOffsets.clear();
            streamInfo.diffByteVal.clear();
            if (identicalBytes == streamInfo.streamLength) { // We have a full match
                fullmatch = true;
            } else { // There are different bytes and/or bytes at the end
                uint64_t last = -1;
                fullmatch = identicalBytes + 2 >= streamInfo.streamLength; // At most 2 bytes diff
                for (i = 0; i < smaller; i++) {
                    if (rBuffer[i] != buffer[i + streamInfo.offset]) { // Mismatching byte is found
                        streamInfo.diffByteOffsets.push_back(i - last - 1);
                        streamInfo.diffByteVal.push_back(buffer[i + streamInfo.offset]);
                        last = i;
                    }
                }
                // If the recompressed stream is shorter add bytes after diffing
                if (strm.total_out < streamInfo.streamLength) {
                    streamInfo.diffByteOffsets.push_back(strm.total_out - last - 1);
                    streamInfo.diffByteVal.push_back(buffer[strm.total_out + streamInfo.offset]);
                    for (i = 1; i < streamInfo.streamLength - strm.total_out; i++) {
                        streamInfo.diffByteOffsets.push_back(0);
                        streamInfo.diffByteVal.push_back(buffer[i + strm.total_out + streamInfo.offset]);
                    }
                }
            }
        }
    }

    // Deallocate the zlib stream and check if it went well
    ret = deflateEnd(&strm);
    if (ret != Z_OK && !skip) {
        std::cout << std::endl << "deflateEnd() failed with exit code:" << ret << std::endl; // Should never happen
        abort();
    }
    delete [] rBuffer;
    return fullmatch;
}

std::vector<StreamInfo> searchBuffer(unsigned char *buffer, uint_fast64_t bufferSize) {
    std::vector<StreamInfo> streamInfoList;
    int recompressed = 0;
    for (int_fast64_t i = 0; i < bufferSize - 1; i++) {
        int header = ((int)buffer[i]) * 256 + (int)buffer[i + 1];
        int offsetType = parseOffsetType(header);
        if (offsetType == -1) {
            // Zip file local file header with deflate compression
            bool zip = false;
            if (i < bufferSize - 29 && buffer[i] == 'P' && buffer[i + 1] == 'K' && buffer[i + 2] == '\x3'
                && buffer[i + 3] == '\x4' && buffer[i + 8] == '\x8' && buffer[i + 9] == '\0') {
                int nameLength = (int)buffer[i + 26] + ((int)buffer[i + 27]) * 256
                               + (int)buffer[i + 28] + ((int)buffer[i + 29]) * 256;
                if (nameLength < 256 && i < bufferSize - 29 - nameLength) {
                    i += 30 + nameLength;
                    zip = true;
                }
            }
            if (!zip) continue;
        }

        // Check offset and find stream length
        uint64_t total_in, total_out;
        bool hasHeader = offsetType != -1;
        if (!checkOffset(&buffer[i], bufferSize - i, total_in, total_out, hasHeader)) continue;
        StreamInfo streamInfo = StreamInfo(i, offsetType, total_in, total_out); // Valid offset found
        std::cout << std::endl << "HDR: 0x" << std::hex << std::setfill('0') << std::setw(4) << header << std::dec << " at " << i;
        std::cout << " (" << streamInfo.inflatedLength << "->" << streamInfo.streamLength << "/" << hasHeader << ")";

        // Find the parameters to use for recompression
        unsigned char* dBuffer = new unsigned char[streamInfo.inflatedLength];
        ZlibWrapper zw(&buffer[streamInfo.offset], streamInfo.streamLength, dBuffer, streamInfo.inflatedLength);
        if (!zw.doInflate(hasHeader)) {
            std::cout << std::endl << "doInflate() failed" << std::endl;
            abort();
        }

        // Try all possible memlevel and clevel parameters
        const int window = hasHeader ? 10 + streamInfo.offsetType / 4 : 0; // Take window size from zlib header
        const int ctype = streamInfo.offsetType % 4;
        const int minclevel = hasHeader ? (ctype == 3 ? 7 :ctype == 2 ? 6 : ctype == 1 ? 2 : 1) : 1;
        const int maxclevel = hasHeader ? (ctype == 3 ? 9 :ctype == 2 ? 6 : ctype == 1 ? 5 : 1) : 9;
        bool fullmatch = false;
        for (int memlevel = 9; memlevel >= 1; memlevel--) {
            for (int clevel = maxclevel; clevel >= minclevel; clevel--) {
                fullmatch = testParameters(buffer, dBuffer, streamInfo, window, memlevel, clevel);
                if (fullmatch) break;
            }
            if (fullmatch) break;
        }
        delete [] dBuffer;
        if (streamInfo.identBytes > 0) streamInfoList.push_back(streamInfo);

        std::cout << (streamInfo.identBytes > 0 ? " OK" : " --") << " [";
        std::cout << (int)streamInfo.memlevel << "," << (int)streamInfo.clevel << "," << (int)streamInfo.window << "]";
        i += total_in - 1; // Skip to the end of zlib stream
    }
    return streamInfoList;
}

void writeNumber1(std::ofstream &outfile, uint8_t number) {
    outfile.write(reinterpret_cast<char*>(&number), 1);
}

void writeNumber8(std::ofstream &outfile, uint64_t number) {
    outfile.write(reinterpret_cast<char*>(&number), 8);
}

void writeStreamInfo(std::ofstream &outfile, unsigned char *buffer, const StreamInfo &streamInfo) {
    writeNumber8(outfile, streamInfo.offset);
    writeNumber8(outfile, streamInfo.streamLength);
    writeNumber8(outfile, streamInfo.inflatedLength);
    writeNumber1(outfile, streamInfo.clevel);
    writeNumber1(outfile, streamInfo.window);
    writeNumber1(outfile, streamInfo.memlevel);
    uint64_t diffBytes = streamInfo.diffByteOffsets.size();
    writeNumber8(outfile, diffBytes);
    for (int_fast64_t i = 0; i < diffBytes; i++) writeNumber8(outfile, streamInfo.diffByteOffsets[i]);
    for (int_fast64_t i = 0; i < diffBytes; i++) writeNumber1(outfile, streamInfo.diffByteVal[i]);

    // decompress zlib stream
    unsigned char* dBuffer = new unsigned char[streamInfo.inflatedLength];
    ZlibWrapper zw(&buffer[streamInfo.offset], streamInfo.streamLength, dBuffer, streamInfo.inflatedLength);
    if (!zw.doInflate(streamInfo.window != 0)) {
        std::cout << std::endl << "doInflate() failed" << std::endl;
        abort();
    }
    outfile.write(reinterpret_cast<char*>(dBuffer), streamInfo.inflatedLength);
    delete [] dBuffer;
}

bool preprocess(const char *infile_name, const char *atzfile_name) {
    std::cout << "Preprocess to ATZ file: " << infile_name << " -> " << atzfile_name << std::endl;

    // Open input file and read contents into the buffer
    std::ifstream infile;
    infile.open(infile_name, std::ios::in | std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        std::cout << "Error: Open file for input failed!" << std::endl;
        return false;
    }
    uint_fast64_t inFileSize = infile.tellg(); // Get the size of the file
    infile.seekg (0, infile.beg);
    unsigned char *buffer = new unsigned char[inFileSize];
    infile.read(reinterpret_cast<char*>(buffer), inFileSize); // Read into the buffer
    infile.close();

    // Search the file for possible zlib streams and analyze them
    std::vector<StreamInfo> streamInfoList = searchBuffer(buffer, inFileSize);
    const int_fast64_t total = streamInfoList.size();
    std::cout << std::endl << "Total streams recompressed: " << total << std::endl;

    // Take the created information and use it to create an ATZ file
    std::ofstream outfile;
    outfile.open(atzfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
        std::cout << "Error: open file for output failed!" << std::endl;
        delete [] buffer;
        return false;
    }
    outfile.write("ATZ\1", 4); // File header and version
    outfile.write("\0\0\0\0\0\0\0\0", 8); // Placeholder for the length of the atz file
    writeNumber8(outfile, inFileSize); // Length of the original file
    writeNumber8(outfile, total); // Number of recompressed streams
    for (int_fast64_t i = 0; i < total; i++) {
        writeStreamInfo(outfile, buffer, streamInfoList[i]);
    }
    uint64_t next = 0;
    for (int_fast64_t i = 0; i < total; i++) { // Non-recompressed data
        if (next != streamInfoList[i].offset) {
            outfile.write(reinterpret_cast<char*>(&buffer[next]), streamInfoList[i].offset - next);
        }
        next = streamInfoList[i].offset + streamInfoList[i].streamLength;
    }
    if (next < inFileSize) { // If there is stuff after the last stream, write that to disk too
        outfile.write(reinterpret_cast<char*>(&buffer[next]), inFileSize - next);
    }

    uint64_t atzFileSize = outfile.tellp();
    std::cout << "Total bytes written: " << atzFileSize << std::endl;
    outfile.seekp(4); // Go back to the placeholder
    writeNumber8(outfile, atzFileSize);

    streamInfoList.clear();
    streamInfoList.shrink_to_fit();
    outfile.close();
    delete [] buffer;
    return true;
}

bool reconstruct(const char *atzfile_name, const char *reconfile_name) {
    std::cout << "Reconstruct from ATZ file: " << atzfile_name << " -> " << reconfile_name << std::endl;

    // Open ATZ file and read contents into the buffer
    std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary | std::ios::ate);
    if (!atzfile.is_open()) {
        std::cout << "Error: opening ATZ file for input failed!" << std::endl;
        return false;
    }
    int_fast64_t inFileSize = atzfile.tellg(); // Read input file size
    atzfile.seekg (0, atzfile.beg);
    unsigned char *buffer = new unsigned char[inFileSize];
    atzfile.read(reinterpret_cast<char*>(buffer), inFileSize);
    atzfile.close();

    if (buffer[0] != 'A' || buffer[1] != 'T' || buffer[2] != 'Z' || buffer[3] != '\1') {
        std::cout << "ATZ1 header not found" << std::endl;
        delete [] buffer;
        return false;
    }
    uint64_t atzFileSize = *reinterpret_cast<uint64_t*>(&buffer[4]); // ATZ file size
    if (atzFileSize != inFileSize){
        std::cout << "Invalid file : ATZ file size mismatch" << std::endl;
        delete [] buffer;
        return false;
    }
    uint64_t originalSize = *reinterpret_cast<uint64_t*>(&buffer[12]); // Original file size
    uint64_t total = *reinterpret_cast<uint64_t*>(&buffer[20]); // Number of streams

    // Read all the info about the streams
    std::vector<StreamInfo> streamInfoList;
    streamInfoList.reserve(total);
    uint64_t last = 28;
    for (int_fast64_t j = 0; j < total; j++) {
        streamInfoList.push_back(StreamInfo(*reinterpret_cast<uint64_t*>(&buffer[last]), -1, // Stream offset
                                            *reinterpret_cast<uint64_t*>(&buffer[8 + last]), // Compressed length
                                            *reinterpret_cast<uint64_t*>(&buffer[16 + last]))); // Decompressed length
        streamInfoList[j].clevel = buffer[24 + last];
        streamInfoList[j].window = buffer[25 + last];
        streamInfoList[j].memlevel = buffer[26 + last];

        // Partial match handling
        uint64_t diffBytes = *reinterpret_cast<uint64_t*>(&buffer[27 + last]);
        streamInfoList[j].diffByteOffsets.reserve(diffBytes);
        streamInfoList[j].diffByteVal.reserve(diffBytes);
        for (int_fast64_t i=0; i < diffBytes; i++){
            streamInfoList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&buffer[35 + 8 * i + last]));
            streamInfoList[j].diffByteVal.push_back(buffer[35 + diffBytes * 8 + i + last]);
        }
        streamInfoList[j].streamData = &buffer[35 + diffBytes * 9 + last]; // Pointer to uncompressed stream data
        last += 35 + diffBytes * 9 + streamInfoList[j].inflatedLength;
    }

    // Do the reconstructing
    uint64_t next = 0;
    std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
    for (int_fast64_t j = 0; j < total; j++) {
        // Write the gap before the stream (if there is one)
        if (next != streamInfoList[j].offset) {
            recfile.write(reinterpret_cast<char*>(&buffer[last]), streamInfoList[j].offset - next);
            last += streamInfoList[j].offset - next;
        }

        // Do the compression using the parameters from the ATZ file
        unsigned char* cBuffer = new unsigned char[streamInfoList[j].streamLength];
        ZlibWrapper zw(streamInfoList[j].streamData, streamInfoList[j].inflatedLength, cBuffer, streamInfoList[j].streamLength);
        zw.doDeflate(streamInfoList[j].clevel, streamInfoList[j].window, streamInfoList[j].memlevel);

        // Modify the compressed data according to the ATZ file (if necessary)
        uint64_t db = streamInfoList[j].diffByteOffsets.size();
        uint64_t sum = -1;
        for(int_fast64_t i = 0; i < db; i++){
            sum += streamInfoList[j].diffByteOffsets[i] + 1;
            cBuffer[sum] = streamInfoList[j].diffByteVal[i];
        }
        recfile.write(reinterpret_cast<char*>(cBuffer), streamInfoList[j].streamLength);
        delete [] cBuffer;
        next = streamInfoList[j].offset + streamInfoList[j].streamLength;
    }
    if (next < originalSize) {
        recfile.write(reinterpret_cast<char*>(&buffer[last]), originalSize - next);
    }
    recfile.close();

    delete [] buffer;
    return true;
}

int main(int argc, char* argv[]) {
    // Parse command line parameters
    if (argc < 2) {
        std::cout << "Error: no input specified" << std::endl;
        std::cout << "Usage: antiz.exe <input file> <switches>" << std::endl;
        std::cout << "Valid switches:" << std::endl;
        std::cout << "-r : assume the input file is an ATZ file, skip to reconstruction" << std::endl;
        return EXIT_FAILURE;
    } else {
        if (argc >= 3 && strcmp(argv[2], "-r") == 0) {
            std::string atzfile(argv[1]);
            std::string reconfile(std::string(atzfile) + ".rec");
            if (!reconstruct(atzfile.c_str(), reconfile.c_str())) return EXIT_FAILURE;
        } else {
            std::string infile(argv[1]);
            std::string atzfile(std::string(argv[1]) + ".atz");
            std::string reconfile(std::string(argv[1]) + ".atz.rec");
            if (!preprocess(infile.c_str(), atzfile.c_str())) return EXIT_FAILURE;
            if (!reconstruct(atzfile.c_str(), reconfile.c_str())) return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}