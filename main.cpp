#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>
#include <iomanip>
#include <cstdlib>
#include <ctime>

class StreamInfo {
public:
    StreamInfo(uint64_t os, int ot, uint64_t sl, uint64_t il) {
        offset = os;
        offsetType = ot;
        streamLength = sl;
        inflatedLength = il;
        clevel = 9;
        window = 15;
        memlvl = 9;
        identBytes = 0;
        firstDiffByte = -1;
        recomp = false;
        atzInfos = 0;
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
    uint8_t memlvl;
    int_fast64_t identBytes;
    int_fast64_t firstDiffByte; // The offset of the first byte that does not match, relative to stream start
    std::vector<int_fast64_t> diffByteOffsets; // Offsets of bytes that differ, this is an incremental offset list
    std::vector<unsigned char> diffByteVal;
    bool recomp;
    unsigned char* atzInfos;
};

// A zlib stream has the following structure: (http://tools.ietf.org/html/rfc1950)
//       +---+---+   CMF: bits 0 to 3  CM      Compression method (8 = deflate)
//       |CMF|FLG|        bits 4 to 7  CINFO   Compression info (base-2 logarithm of the LZ77 window size, minus 8)
//       +---+---+
//                   FLG: bits 0 to 4  FCHECK  Check bits for CMF and FLG (in MSB order (CMF*256 + FLG), is a multiple of 31)
//                        bit  5       FDICT   Preset dictionary
//                        bits 6 to 7  FLEVEL  Compression level (0 = fastest, 1 = fast, 2 = default, 3 = maximum)
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

bool checkOffset(unsigned char *next_in, uint64_t avail_in, uint64_t &total_in, uint64_t &total_out) {
    z_stream strm;
    bool success = false;
    int_fast64_t memScale = 1;
    while (true) {
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = avail_in;
        strm.next_in = next_in;

        // Initialize the stream for decompression and check for error
        if (inflateInit(&strm) != Z_OK) return false;
            
        // A buffer needs to be created to hold the resulting decompressed data. This is a big
        // problem since the zlib header does not contain the length of the decompressed data.
        // The best we can do is to take a guess, and see if it was big enough, if not then scale it up.
        unsigned char* decompBuffer = new unsigned char[memScale * 5 * avail_in]; // Just a wild guess
        strm.next_out = decompBuffer;
        strm.avail_out = memScale * 5 * avail_in;
        int ret=inflate(&strm, Z_FINISH); // Try to do the actual decompression in one pass
        if (ret == Z_STREAM_END && strm.total_in >= 16) // Decompression was succesful
        {
            total_in = strm.total_in;
            total_out = strm.total_out;
            success = true;
        }
        if (inflateEnd(&strm) != Z_OK) success = false;
        delete [] decompBuffer;
        if (ret != Z_BUF_ERROR) break;
        memScale++; // Increase buffer size for the next iteration
    }
    return success;
}

void searchBuffer(unsigned char *buffer, uint_fast64_t bufferSize, std::vector<StreamInfo> &streamInfoList) {
    for (int_fast64_t i = 0; i < bufferSize - 1; i++){
        int header = ((int)buffer[i]) * 256 + (int)buffer[i + 1];
        int offsetType = parseOffsetType(header);
        if (offsetType >= 0) {
            #ifdef debug
            std::cout << "Zlib header 0x" << std::hex << std::setfill('0') << std::setw(4) << header << std::dec
                      << " with " << (1 << ((header >> 12) - 2)) << "K window at offset: " << i << std::endl;
            #endif // debug
            uint64_t total_in, total_out;
            if (checkOffset(&buffer[i], bufferSize - i, total_in, total_out)) {
                streamInfoList.push_back(StreamInfo(i, offsetType, total_in, total_out)); // Valid offset found
                i += total_in - 1; // Skip to the end of zlib stream
            }
        }
    }
}

bool testParameters(unsigned char *buffer, unsigned char *decompBuffer, StreamInfo &streamInfo, int window, int memlevel, int clevel)  {
    const int recompTresh = 128; // Recompressed if differs from the original in <= recompTresh bytes
    const int diffTresh = 128; // Compared when the size difference is <= diffTresh
    const int testBlock = 1024; // Data are compressed to testBlock bytes first before full compression

    // Use default settings except window, clevel and memlevel
    z_stream strm1;
    strm1.zalloc = Z_NULL;
    strm1.zfree = Z_NULL;
    strm1.opaque = Z_NULL;
    strm1.next_in = decompBuffer;
    int ret = deflateInit2(&strm1, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        std::cout << std::endl << "deflateInit() failed with exit code:" << ret << std::endl; // Should never happen normally
        abort();
    }
    const int recompSize = deflateBound(&strm1, streamInfo.inflatedLength); // Allocate buffer for worst case
    unsigned char* recompBuffer = new unsigned char[recompSize];
    strm1.avail_out = std::min(recompSize, testBlock);
    strm1.avail_in = streamInfo.inflatedLength;
    strm1.next_out = recompBuffer;
    ret=deflate(&strm1, Z_FINISH); // Do the partial compression (first testBlock bytes)
    bool skip = false;
    if (ret == Z_OK && recompSize > testBlock) {
        // Check if full recompression is needed
        if (strm1.total_out == testBlock && streamInfo.streamLength >= testBlock) {
            int identicalBytes = 0;
            for (int i = 0; i < testBlock ; i++) {
                if (recompBuffer[i] == buffer[i + streamInfo.offset]) identicalBytes++;
            }
            skip = identicalBytes < testBlock - recompTresh;
        }
        if (!skip) {
            strm1.avail_out = recompSize;
            ret = deflate(&strm1, Z_FINISH); // Do full recompression
        }
    }
    if (ret != Z_STREAM_END && (!skip || ret != Z_OK)){
        std::cout<<std::endl<<"recompression failed with exit code:"<<ret<<std::endl;
        abort();
    }
    
    bool fullmatch = false;
    int_fast64_t i;
    //test if the recompressed stream matches the input data
    if (skip || strm1.total_out!=streamInfo.streamLength){
        int_fast64_t identicalBytes=0;
        //std::cout<<"   size difference: "<<(strm1.total_out-static_cast<int64_t>(streamInfo.streamLength))<<std::endl;
        if (skip || abs((strm1.total_out-streamInfo.streamLength))>diffTresh){
        } else {
            if (strm1.total_out<streamInfo.streamLength){
                for (i=0; i<strm1.total_out;i++){
                    if (recompBuffer[i]==buffer[(i+streamInfo.offset)]){
                        identicalBytes++;
                    }
                }
            } else {
                for (i=0; i<streamInfo.streamLength;i++){
                    if (recompBuffer[i]==buffer[(i+streamInfo.offset)]){
                        identicalBytes++;
                    }
                }
            }
            if (identicalBytes>streamInfo.identBytes){//if this recompressed stream has more matching bytes than the previous best
                streamInfo.identBytes=identicalBytes;
                streamInfo.clevel=clevel;
                streamInfo.memlvl=memlevel;
                streamInfo.window=window;
                streamInfo.firstDiffByte=-1;
                streamInfo.diffByteOffsets.clear();
                streamInfo.diffByteVal.clear();
                int_fast64_t last_i=0;
                if (strm1.total_out<streamInfo.streamLength){//the recompressed stream is shorter than the original
                    for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                        if (recompBuffer[i]!=buffer[(i+streamInfo.offset)]){//if a mismatching byte is found
                            if (streamInfo.firstDiffByte<0){//if the first different byte is negative, then this is the first
                                streamInfo.firstDiffByte=(i);
                                streamInfo.diffByteOffsets.push_back(0);
                                streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                                last_i=i;
                            } else {
                                streamInfo.diffByteOffsets.push_back(i-last_i);
                                streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                                //std::cout<<"   different byte:"<<i<<std::endl;
                                last_i=i;
                            }
                        }
                    }
                    for (i=0; i<(streamInfo.streamLength-strm1.total_out); i++){
                        if ((i==0)&&((last_i+1)<strm1.total_out)){
                            streamInfo.diffByteOffsets.push_back(strm1.total_out-last_i);
                        } else{
                            streamInfo.diffByteOffsets.push_back(1);
                        }
                        streamInfo.diffByteVal.push_back(buffer[(i+strm1.total_out+streamInfo.offset)]);
                    }
                } else {//the recompressed stream is longer than the original
                    for (i=0; i<streamInfo.streamLength;i++){
                        if (recompBuffer[i]!=buffer[(i+streamInfo.offset)]){//if a mismatching byte is found
                            if (streamInfo.firstDiffByte<0){//if the first different byte is negative, then this is the first
                                streamInfo.firstDiffByte=(i);
                                streamInfo.diffByteOffsets.push_back(0);
                                streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                                last_i=i;
                            } else {
                                streamInfo.diffByteOffsets.push_back(i-last_i);
                                streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                                //std::cout<<"   different byte:"<<i<<std::endl;
                                last_i=i;
                            }
                        }
                    }
                }
            }
        }
    } else {
        int_fast64_t identicalBytes=0;
        for (i=0; i<strm1.total_out;i++){
            if (recompBuffer[i]==buffer[(i+streamInfo.offset)]){
                identicalBytes++;
            }
        }
        if (identicalBytes==streamInfo.streamLength){
            fullmatch=true;
            streamInfo.identBytes=identicalBytes;
            streamInfo.clevel=clevel;
            streamInfo.memlvl=memlevel;
            streamInfo.window=window;
            streamInfo.firstDiffByte=-1;
            streamInfo.diffByteOffsets.clear();
            streamInfo.diffByteVal.clear();
        } else {
            if (((streamInfo.streamLength-identicalBytes)==2)&&((recompBuffer[0]-buffer[streamInfo.offset])!=0)&&((recompBuffer[1]-buffer[(1+streamInfo.offset)])!=0)){
                fullmatch=true;
                streamInfo.identBytes=identicalBytes;
                streamInfo.clevel=clevel;
                streamInfo.memlvl=memlevel;
                streamInfo.window=window;
                streamInfo.firstDiffByte=0;
                streamInfo.diffByteOffsets.clear();
                streamInfo.diffByteVal.clear();
                streamInfo.diffByteOffsets.push_back(0);
                streamInfo.diffByteOffsets.push_back(1);
                streamInfo.diffByteVal.push_back(buffer[streamInfo.offset]);
                streamInfo.diffByteVal.push_back(buffer[(1+streamInfo.offset)]);
            }
            if (((streamInfo.streamLength-identicalBytes)==1)&&(((recompBuffer[0]-buffer[streamInfo.offset])!=0)||((recompBuffer[1]-buffer[(1+streamInfo.offset)])!=0))){
                fullmatch=true;
                streamInfo.identBytes=identicalBytes;
                streamInfo.clevel=clevel;
                streamInfo.memlvl=memlevel;
                streamInfo.window=window;
                streamInfo.diffByteOffsets.clear();
                streamInfo.diffByteVal.clear();
                if (recompBuffer[0]!=buffer[streamInfo.offset]){
                    streamInfo.firstDiffByte=0;
                    streamInfo.diffByteVal.push_back(buffer[streamInfo.offset]);
                } else {
                    streamInfo.firstDiffByte=1;
                    streamInfo.diffByteVal.push_back(buffer[(1+streamInfo.offset)]);
                }
                streamInfo.diffByteOffsets.push_back(0);
            }
            if ((identicalBytes>streamInfo.identBytes)&&!fullmatch){
                streamInfo.identBytes=identicalBytes;
                streamInfo.clevel=clevel;
                streamInfo.memlvl=memlevel;
                streamInfo.window=window;
                streamInfo.firstDiffByte=-1;
                streamInfo.diffByteOffsets.clear();
                streamInfo.diffByteVal.clear();
                int_fast64_t last_i=0;
                for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                    if (recompBuffer[i]!=buffer[(i+streamInfo.offset)]){//if a mismatching byte is found
                        if (streamInfo.firstDiffByte<0){//if the first different byte is negative, then this is the first
                            streamInfo.firstDiffByte=(i);
                            streamInfo.diffByteOffsets.push_back(0);
                            streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                            last_i=i;
                        } else {
                            streamInfo.diffByteOffsets.push_back(i-last_i);
                            streamInfo.diffByteVal.push_back(buffer[(i+streamInfo.offset)]);
                            last_i=i;
                        }
                    }
                }
            }
        }
    }
    streamInfo.recomp = ((streamInfo.streamLength - streamInfo.identBytes <= recompTresh) && (streamInfo.identBytes > 0));
    
    // Deallocate the zlib stream and check if it went well
    ret=deflateEnd(&strm1);
    if (ret != Z_OK && !skip)
    {
        std::cout<<std::endl<<"deflateEnd() failed with exit code:"<<ret<<std::endl; // Should never happen normally
        abort();
    }
    delete [] recompBuffer;
    return fullmatch;
}

bool analyzeStream(unsigned char *buffer, uint_fast64_t bufferSize, StreamInfo &streamInfo) {
    std::clock_t begin = std::clock();

    // Reset and initialize the zlib stream to do decompression
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = streamInfo.streamLength;
    strm.next_in = &buffer[streamInfo.offset];
    int ret = inflateInit(&strm);
    if (ret != Z_OK)
    {
        std::cout << std::endl << "inflateInit() failed with exit code:" << ret << std::endl; // Should never happen normally
        abort();
    }
    
    // A buffer needs to be created to hold the resulting decompressed data. Since we have already
    // deompressed the data before, we know exactly how large of a buffer we need to allocate.
    unsigned char* decompBuffer = new unsigned char[streamInfo.inflatedLength];
    strm.next_out = decompBuffer;
    strm.avail_out = streamInfo.inflatedLength;
    ret=inflate(&strm, Z_FINISH); // Actual decompression in one pass
    if (ret != Z_STREAM_END) {
        std::cout << std::endl << "inflate() failed with exit code:" << ret << std::endl;
        abort();
    }

    // Try all possible memlevel and clevel parameters
    const int window = 10 + streamInfo.offsetType / 4; // Take window size from zlib header
    bool fullmatch = false;
    for (int memlevel = 9; memlevel > 0; memlevel--) {
        for (int clevel = 9; clevel > 0; clevel--) {
            fullmatch = testParameters(buffer, decompBuffer, streamInfo, window, memlevel, clevel);
            if (fullmatch) break;
        }
        if (fullmatch) break;
    }

    // Deallocate the zlib stream, check for errors and deallocate the decompression buffer
    ret = inflateEnd(&strm);
    if (ret != Z_OK) {
        std::cout << std::endl << "inflateEnd() failed with exit code:" << ret << std::endl; // Should never happen normally
        abort();
    }
    delete [] decompBuffer;
    
    const double elapsed_secs = double(std::clock() - begin) / CLOCKS_PER_SEC;
    std::cout << " - time: " << elapsed_secs << " s.";
    std::cout << " > " << streamInfo.identBytes
              << " |"<<(int)streamInfo.memlvl << "|" << (int)streamInfo.clevel << "|"
              << (int)streamInfo.window << "|";

    return streamInfo.recomp;
}

bool preprocess(const char *infile_name, const char *atzfile_name) {
    std::cout << "Preprocess to ATZ file: " << infile_name << " -> " << atzfile_name << std::endl;

    // Open file and read contents into the buffer
    std::ifstream infile;    
    infile.open(infile_name, std::ios::in | std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        std::cout << "Error: Open file for input failed!" << std::endl;
        return false;
    }
    uint_fast64_t infileSize = infile.tellg(); // Get the size of the file
    infile.seekg (0, infile.beg);
    unsigned char *buffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(buffer), infileSize); // Read into the buffer
    infile.close();

    // Search the file for possible zlib streams and try to decompress them
    std::vector<StreamInfo> streamInfoList;
    searchBuffer(buffer, infileSize, streamInfoList);
    std::cout << "Good offsets: " << streamInfoList.size() << std::endl;

    // Find the parameters to use for recompression
    const int_fast64_t total = streamInfoList.size();
    int_fast64_t recompressed = 0;    
    for (int_fast64_t i = 0; i < total; i++) {
        std::cout << std::endl << "Offset " << i << " / " << total
                  << " (c:" << streamInfoList[i].streamLength << ",d:" << streamInfoList[i].inflatedLength << ")";
        recompressed += analyzeStream(buffer, infileSize, streamInfoList[i]);
    }
    std::cout << std::endl << "Recompressed " << recompressed << " / " << total << std::endl;


    //PHASE 4
    //take the information created in phase 3 and use it to create an ATZ file(see ATZ file format spec.)
    std::ofstream outfile;
    outfile.open(atzfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
       std::cout << "error: open file for output failed!" << std::endl;
        abort();
    }
    {//write file header and version
        unsigned char* atz1=new unsigned char[4];
        atz1[0]=65;
        atz1[1]=84;
        atz1[2]=90;
        atz1[3]=1;
        outfile.write(reinterpret_cast<char*>(atz1), 4);
        delete [] atz1;
    }

    uint64_t atzlen=0;//placeholder for the length of the atz file
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    outfile.write(reinterpret_cast<char*>(&infileSize), 8);//the length of the original file
    outfile.write(reinterpret_cast<char*>(&recompressed), 8);//number of recompressed streams

    for(int_fast64_t j=0;j<total;j++){//write recompressed stream descriptions
        if (streamInfoList[j].recomp){//we are operating on the j-th stream
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].offset), 8);
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].streamLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].inflatedLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].clevel), 1);
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].window), 1);
            outfile.write(reinterpret_cast<char*>(&streamInfoList[j].memlvl), 1);

            {
                uint64_t diffbytes=streamInfoList[j].diffByteOffsets.size();
                outfile.write(reinterpret_cast<char*>(&diffbytes), 8);
                if (diffbytes>0){
                    uint64_t firstdiff=streamInfoList[j].firstDiffByte;
                    outfile.write(reinterpret_cast<char*>(&firstdiff), 8);
                    uint64_t diffos;
                    uint8_t diffval;
                    for(int_fast64_t i=0;i<diffbytes;i++){
                        diffos=streamInfoList[j].diffByteOffsets[i];
                        outfile.write(reinterpret_cast<char*>(&diffos), 8);
                    }
                    for(int_fast64_t i=0;i<diffbytes;i++){
                        diffval=streamInfoList[j].diffByteVal[i];
                        outfile.write(reinterpret_cast<char*>(&diffval), 1);
                    }
                }
            }

            //create a new Zlib stream to do decompression
            z_stream strm;
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in= streamInfoList[j].streamLength;
            strm.next_in=buffer+streamInfoList[j].offset;
            //initialize the stream for decompression and check for error
            int ret=inflateInit(&strm);
            if (ret != Z_OK)
            {
                std::cout<<"inflateInit() failed with exit code:"<<ret<<std::endl;
                abort();
            }
            //a buffer needs to be created to hold the resulting decompressed data
            unsigned char* decompBuffer= new unsigned char[streamInfoList[j].inflatedLength];
            strm.next_out=decompBuffer;
            strm.avail_out=streamInfoList[j].inflatedLength;
            ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
            //check the return value
            switch (ret)
            {
                case Z_STREAM_END://decompression was succesful
                {
                    break;
                }
                default://shit hit the fan, should never happen normally
                {
                    std::cout<<"inflate() failed with exit code:"<<ret<<std::endl;
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                std::cout<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
                return ret;
            }
            outfile.write(reinterpret_cast<char*>(decompBuffer), streamInfoList[j].inflatedLength);
            delete [] decompBuffer;
        }
    }

    uint64_t lastos=0, lastlen=0;
    for(int_fast64_t j=0;j<streamInfoList.size();j++){//write the gaps before streams and non-recompressed streams to disk as the residue
        if ((lastos+lastlen)==streamInfoList[j].offset){
            if (streamInfoList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(buffer+streamInfoList[j].offset), streamInfoList[j].streamLength);
            }
        }else{
            outfile.write(reinterpret_cast<char*>(buffer+lastos+lastlen), (streamInfoList[j].offset-(lastos+lastlen)));
            if (streamInfoList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(buffer+streamInfoList[j].offset), streamInfoList[j].streamLength);
            }
        }
        lastos=streamInfoList[j].offset;
        lastlen=streamInfoList[j].streamLength;
    }
    if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
        outfile.write(reinterpret_cast<char*>(buffer+lastos+lastlen), (infileSize-(lastos+lastlen)));
    }

    atzlen=outfile.tellp();
    std::cout<<"Total bytes written: "<<atzlen<<std::endl;
    outfile.seekp(4);//go back to the placeholder
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    streamInfoList.clear();
    streamInfoList.shrink_to_fit();
    outfile.close();
    delete [] buffer;
    return true;
}

bool reconstruct(const char *atzfile_name, const char *reconfile_name) {
    int_fast64_t i, j;    
    std::cout << "Reconstruct from ATZ file: " << atzfile_name << " -> " << reconfile_name << std::endl;
    
    //PHASE 5: verify that we can reconstruct the original file, using only data from the ATZ file
    uint64_t lastos=28, lastlen;
    uint64_t origlen=0;
    uint64_t nstrms=0;

    std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary | std::ios::ate);
    if (!atzfile.is_open()) {
       std::cout << "error: open ATZ file for input failed!" << std::endl;
        abort();
    }
    std::cout<<"reconstructing from "<<atzfile_name<<std::endl;
    int_fast64_t infileSize=atzfile.tellg();
    atzfile.seekg (0, atzfile.beg);
    std::cout<<"File size:"<<infileSize<<std::endl;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char *buffer = new unsigned char[infileSize];
    atzfile.read(reinterpret_cast<char*>(buffer), infileSize);
    atzfile.close();

    if ((buffer[0]!=65)||(buffer[1]!=84)||(buffer[2]!=90)||(buffer[3]!=1)){
        std::cout<<"ATZ1 header not found"<<std::endl;
        abort();
    }
    uint64_t atzlen=*reinterpret_cast<uint64_t*>(&buffer[4]);
    if (atzlen!=infileSize){
        std::cout<<"atzlen mismatch"<<std::endl;
        abort();
    }
    origlen=*reinterpret_cast<uint64_t*>(&buffer[12]);
    nstrms=*reinterpret_cast<uint64_t*>(&buffer[20]);
    if (nstrms>0){
        std::vector<StreamInfo> streamInfoList;
        streamInfoList.reserve(nstrms);
        //reead in all the info about the streams
        for (j=0;j<nstrms;j++){
            streamInfoList.push_back(StreamInfo(*reinterpret_cast<uint64_t*>(&buffer[lastos]), -1, *reinterpret_cast<uint64_t*>(&buffer[8+lastos]), *reinterpret_cast<uint64_t*>(&buffer[16+lastos])));
            streamInfoList[j].clevel=buffer[24+lastos];
            streamInfoList[j].window=buffer[25+lastos];
            streamInfoList[j].memlvl=buffer[26+lastos];
            //partial match handling
            uint64_t diffbytes=*reinterpret_cast<uint64_t*>(&buffer[27+lastos]);
            if (diffbytes>0){//if the stream is just a partial match
                streamInfoList[j].firstDiffByte=*reinterpret_cast<uint64_t*>(&buffer[35+lastos]);
                streamInfoList[j].diffByteOffsets.reserve(diffbytes);
                streamInfoList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamInfoList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&buffer[43+8*i+lastos]));
                    streamInfoList[j].diffByteVal.push_back(buffer[43+diffbytes*8+i+lastos]);
                }
                streamInfoList[j].atzInfos=&buffer[43+diffbytes*9+lastos];
                lastos=lastos+43+diffbytes*9+streamInfoList[j].inflatedLength;
            } else{//if the stream is a full match
                streamInfoList[j].firstDiffByte=-1;//negative value signals full match
                streamInfoList[j].atzInfos=&buffer[35+lastos];
                lastos=lastos+35+streamInfoList[j].inflatedLength;
            }
        }
        uint64_t residueos=lastos;
        uint64_t gapsum=0;
        //do the reconstructing
        lastos=0;
        lastlen=0;
        std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
        //write the gap before the stream(if the is one), then do the compression using the parameters from the ATZ file
        //then modify the compressed data according to the ATZ file(if necessary)
        for(j=0;j<streamInfoList.size();j++){
            if ((lastos+lastlen)==streamInfoList[j].offset){//no gap before the stream
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamInfoList[j].streamLength+32768];
                {
                    //do compression
                    z_stream strm;
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamInfoList[j].atzInfos;
                    strm.avail_in=streamInfoList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    int ret=deflateInit2(&strm, streamInfoList[j].clevel, Z_DEFLATED, streamInfoList[j].window, streamInfoList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamInfoList[j].streamLength+32768;
                    ret=deflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
                    //check the return value
                    switch (ret)
                    {
                        case Z_STREAM_END://decompression was succesful
                        {
                            break;
                        }
                        default://shit hit the fan, should never happen normally
                        {
                            std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
                        return ret;
                    }
                }
                //do stream modification if needed
                if (streamInfoList[j].firstDiffByte>=0){
                    uint64_t db=streamInfoList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamInfoList[j].firstDiffByte+streamInfoList[j].diffByteOffsets[i]+sum]=streamInfoList[j].diffByteVal[i];
                        sum=sum+streamInfoList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamInfoList[j].streamLength);
                delete [] compBuffer;
            }else{
                recfile.write(reinterpret_cast<char*>(buffer+residueos+gapsum), (streamInfoList[j].offset-(lastos+lastlen)));
                gapsum=gapsum+(streamInfoList[j].offset-(lastos+lastlen));
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamInfoList[j].streamLength+32768];
                {
                    //do compression
                    z_stream strm;
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamInfoList[j].atzInfos;
                    strm.avail_in=streamInfoList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    int ret=deflateInit2(&strm, streamInfoList[j].clevel, Z_DEFLATED, streamInfoList[j].window, streamInfoList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamInfoList[j].streamLength+32768;
                    ret=deflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
                    //check the return value
                    switch (ret)
                    {
                        case Z_STREAM_END://decompression was succesful
                        {
                            break;
                        }
                        default://shit hit the fan, should never happen normally
                        {
                            std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
                        return ret;
                    }
                }
                //do stream modification if needed
                if (streamInfoList[j].firstDiffByte>=0){
                    uint64_t db=streamInfoList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamInfoList[j].firstDiffByte+streamInfoList[j].diffByteOffsets[i]+sum]=streamInfoList[j].diffByteVal[i];
                        sum=sum+streamInfoList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamInfoList[j].streamLength);
                delete [] compBuffer;
            }
            lastos=streamInfoList[j].offset;
            lastlen=streamInfoList[j].streamLength;
        }
        if ((lastos+lastlen)<origlen){
            recfile.write(reinterpret_cast<char*>(buffer+residueos+gapsum), (origlen-(lastos+lastlen)));
        }
        recfile.close();
    }else{//if there are no recompressed streams
        std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
        recfile.write(reinterpret_cast<char*>(buffer+28), origlen);
        recfile.close();
    }

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