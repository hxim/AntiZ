#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>
#include <iomanip>
#include <cstdlib>
#include <ctime>

class streamOffset {
public:
    streamOffset(uint64_t os, int ot, uint64_t sl, uint64_t il) {
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
    ~streamOffset() {
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
    int_fast64_t firstDiffByte; //the offset of the first byte that does not match, relative to stream start, not file start
    std::vector<int_fast64_t> diffByteOffsets; //offsets of bytes that differ, this is an incremental offset list to enhance recompression, kinda like a PNG filter
    //this improves compression if the mismatching bytes are consecutive, eg. 451,452,453,...(no repetitions, hard to compress)
    //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
    std::vector<unsigned char> diffByteVal;
    bool recomp;
    unsigned char* atzInfos;
};


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

        //initialize the stream for decompression and check for error
        if (inflateInit(&strm) != Z_OK) return false;
            
        //a buffer needs to be created to hold the resulting decompressed data
        //this is a big problem since the zlib header does not contain the length of the decompressed data
        //the best we can do is to take a guess, and see if it was big enough, if not then scale it up
        unsigned char* decompBuffer = new unsigned char[memScale * 5 * avail_in]; //just a wild guess, corresponds to a compression ratio of 20%
        strm.next_out = decompBuffer;
        strm.avail_out = memScale * 5 * avail_in;
        int ret=inflate(&strm, Z_FINISH); //try to do the actual decompression in one pass
        if (ret == Z_STREAM_END && strm.total_in >= 16) //decompression was succesful
        {
            total_in = strm.total_in;
            total_out = strm.total_out;
            success = true;
        }
        if (inflateEnd(&strm) != Z_OK) success = false;
        delete [] decompBuffer;
        if (ret != Z_BUF_ERROR) break;
        memScale++; //increase buffer size for the next iteration
    }
    return success;
}

bool preprocess(const char *infile_name, const char *atzfile_name) {
    int_fast64_t i, j;    
    std::cout << "Preprocess to ATZ file: " << infile_name << " -> " << atzfile_name << std::endl;

    // Open file and read contents into the buffer
    std::ifstream infile;    
    infile.open(infile_name, std::ios::in | std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        std::cout << "Error: Open file for input failed!" << std::endl;
        return false;
    }
    int_fast64_t infileSize = infile.tellg(); // Get the size of the file
    infile.seekg (0, infile.beg);
    unsigned char *buffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(buffer), infileSize); // Read into the buffer
    infile.close();

    // Search the file for possible zlib headers and try to decompress streams
    std::vector<streamOffset> streamOffsetList;
    std::cout<<std::endl;
    for (i = 0; i < infileSize - 1; i++){
        int header = ((int)buffer[i]) * 256 + (int)buffer[i + 1];
        int offsetType = parseOffsetType(header);
        if (offsetType >= 0) {
            #ifdef debug
            std::cout << "Zlib header 0x" << std::hex << std::setfill('0') << std::setw(4) << header << std::dec
                      << " with " << (1 << ((header >> 12) - 2)) << "K window at offset: " << i << std::endl;
            #endif // debug
            uint64_t total_in, total_out;
            if (checkOffset(&buffer[i], infileSize - i, total_in, total_out)) {
                streamOffsetList.push_back(streamOffset(i, offsetType, total_in, total_out)); // Valid offset found
                i += total_in - 1; // Skip to the end of zlib stream
            }
        }
    }
    std::cout << "Good offsets: " << streamOffsetList.size() << std::endl << std::endl;

    //PHASE 3
    //start trying to find the parameters to use for recompression
    const int recompTresh=128;//recompressed if the best match differs from the original in <= recompTresh bytes
    const int sizediffTresh=128;//compared when the size difference is <= sizediffTresh
    int ret=-9;
    z_stream strm, strm1;
    const int_fast64_t numGoodOffsets = streamOffsetList.size();
    bool fullmatch=false;
    std::clock_t begin, end;
    for (j=0; j<numGoodOffsets; j++){
        std::cout << std::endl << "Offset " << j << " / " << numGoodOffsets << " (c:" << streamOffsetList[j].streamLength
        << ",d:" << streamOffsetList[j].inflatedLength << ")";
        begin = std::clock();
        
        
        //reset the Zlib stream to do decompression
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        fullmatch = false;
        int window = 10 + streamOffsetList[j].offsetType / 4; // take window size from zlib header
        int memlevel, clevel;
        //the lengths of the zlib streams have been saved by the previous phase
        strm.avail_in = streamOffsetList[j].streamLength;
        strm.next_in=buffer+streamOffsetList[j].offset;//this is effectively adding an integer to a pointer, resulting in a pointer
        //initialize the stream for decompression and check for error
        ret=inflateInit(&strm);
        if (ret != Z_OK)
        {
            std::cout<<std::endl<<"inflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        //a buffer needs to be created to hold the resulting decompressed data
        //since we have already deompressed the data before, we know exactly how large of a buffer we need to allocate
        unsigned char* decompBuffer= new unsigned char[streamOffsetList[j].inflatedLength];
        strm.next_out=decompBuffer;
        strm.avail_out=streamOffsetList[j].inflatedLength;
        ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        //check the return value
        switch (ret){
            case Z_STREAM_END: //decompression was succesful
            {
                memlevel=9;
                do {
                    clevel=9;
                    do {
                        //resetting the variables
                        strm1.zalloc = Z_NULL;
                        strm1.zfree = Z_NULL;
                        strm1.opaque = Z_NULL;
                        strm1.next_in=decompBuffer;
                        //use all default settings except clevel and memlevel
                        ret = deflateInit2(&strm1, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
                        if (ret != Z_OK)
                        {
                            std::cout<<std::endl<<"deflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
                            abort();
                        }

                        //prepare for compressing in one pass
                        strm1.avail_in=streamOffsetList[j].inflatedLength;
                        unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                        strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                        bool fast = false;
                        bool skip = false;
                        if (strm1.avail_out > 1024) {strm1.avail_out = 1024;fast=true;}
                        strm1.next_out=recompBuffer;
                        ret=deflate(&strm1, Z_FINISH);//do the actual compression
                        //check the return value to see if everything went well
                        if (ret == Z_OK && fast) {
                            int identicalBytes = 1024;
                            if (strm1.total_out == 1024 && streamOffsetList[j].streamLength >= 1024) {
                                identicalBytes = 0;
                                for (i=0; i<1024;i++){
                                    if (recompBuffer[i]==buffer[(i+streamOffsetList[j].offset)]){
                                        identicalBytes++;
                                    }
                                }
                            }
                            if (identicalBytes < 1024 - recompTresh) {
                                skip=true;
                            } else {
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                            }
                        }
                        
                        if (ret != Z_STREAM_END && (!skip || ret != Z_OK)){
                            std::cout<<std::endl<<"recompression failed with exit code:"<<ret<<std::endl;
                            abort();
                        }

                        //test if the recompressed stream matches the input data
                        if (strm1.total_out!=streamOffsetList[j].streamLength || skip){
                            int_fast64_t identicalBytes=0;
                            //std::cout<<"   size difference: "<<(strm1.total_out-static_cast<int64_t>(streamOffsetList[j].streamLength))<<std::endl;
                            if (skip || abs((strm1.total_out-streamOffsetList[j].streamLength))>sizediffTresh){
                            } else {
                                if (strm1.total_out<streamOffsetList[j].streamLength){
                                    for (i=0; i<strm1.total_out;i++){
                                        if (recompBuffer[i]==buffer[(i+streamOffsetList[j].offset)]){
                                            identicalBytes++;
                                        }
                                    }
                                } else {
                                    for (i=0; i<streamOffsetList[j].streamLength;i++){
                                        if (recompBuffer[i]==buffer[(i+streamOffsetList[j].offset)]){
                                            identicalBytes++;
                                        }
                                    }
                                }
                                if (identicalBytes>streamOffsetList[j].identBytes){//if this recompressed stream has more matching bytes than the previous best
                                    streamOffsetList[j].identBytes=identicalBytes;
                                    streamOffsetList[j].clevel=clevel;
                                    streamOffsetList[j].memlvl=memlevel;
                                    streamOffsetList[j].window=window;
                                    streamOffsetList[j].firstDiffByte=-1;
                                    streamOffsetList[j].diffByteOffsets.clear();
                                    streamOffsetList[j].diffByteVal.clear();
                                    int_fast64_t last_i=0;
                                    if (strm1.total_out<streamOffsetList[j].streamLength){//the recompressed stream is shorter than the original
                                        for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                                            if (recompBuffer[i]!=buffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                    streamOffsetList[j].firstDiffByte=(i);
                                                    streamOffsetList[j].diffByteOffsets.push_back(0);
                                                    streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                    last_i=i;
                                                } else {
                                                    streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                    streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                    //std::cout<<"   different byte:"<<i<<std::endl;
                                                    last_i=i;
                                                }
                                            }
                                        }
                                        for (i=0; i<(streamOffsetList[j].streamLength-strm1.total_out); i++){
                                            if ((i==0)&&((last_i+1)<strm1.total_out)){
                                                streamOffsetList[j].diffByteOffsets.push_back(strm1.total_out-last_i);
                                            } else{
                                                streamOffsetList[j].diffByteOffsets.push_back(1);
                                            }
                                            streamOffsetList[j].diffByteVal.push_back(buffer[(i+strm1.total_out+streamOffsetList[j].offset)]);
                                        }
                                    } else {//the recompressed stream is longer than the original
                                        for (i=0; i<streamOffsetList[j].streamLength;i++){
                                            if (recompBuffer[i]!=buffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                    streamOffsetList[j].firstDiffByte=(i);
                                                    streamOffsetList[j].diffByteOffsets.push_back(0);
                                                    streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                    last_i=i;
                                                } else {
                                                    streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                    streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                    //std::cout<<"   different byte:"<<i<<std::endl;
                                                    last_i=i;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            clevel--;
                        } else {
                            int_fast64_t identicalBytes=0;
                            for (i=0; i<strm1.total_out;i++){
                                if (recompBuffer[i]==buffer[(i+streamOffsetList[j].offset)]){
                                    identicalBytes++;
                                }
                            }
                            if (identicalBytes==streamOffsetList[j].streamLength){
                                fullmatch=true;
                                streamOffsetList[j].identBytes=identicalBytes;
                                streamOffsetList[j].clevel=clevel;
                                streamOffsetList[j].memlvl=memlevel;
                                streamOffsetList[j].window=window;
                                streamOffsetList[j].firstDiffByte=-1;
                                streamOffsetList[j].diffByteOffsets.clear();
                                streamOffsetList[j].diffByteVal.clear();
                            } else {
                                if (((streamOffsetList[j].streamLength-identicalBytes)==2)&&((recompBuffer[0]-buffer[streamOffsetList[j].offset])!=0)&&((recompBuffer[1]-buffer[(1+streamOffsetList[j].offset)])!=0)){
                                    fullmatch=true;
                                    streamOffsetList[j].identBytes=identicalBytes;
                                    streamOffsetList[j].clevel=clevel;
                                    streamOffsetList[j].memlvl=memlevel;
                                    streamOffsetList[j].window=window;
                                    streamOffsetList[j].firstDiffByte=0;
                                    streamOffsetList[j].diffByteOffsets.clear();
                                    streamOffsetList[j].diffByteVal.clear();
                                    streamOffsetList[j].diffByteOffsets.push_back(0);
                                    streamOffsetList[j].diffByteOffsets.push_back(1);
                                    streamOffsetList[j].diffByteVal.push_back(buffer[streamOffsetList[j].offset]);
                                    streamOffsetList[j].diffByteVal.push_back(buffer[(1+streamOffsetList[j].offset)]);
                                }
                                if (((streamOffsetList[j].streamLength-identicalBytes)==1)&&(((recompBuffer[0]-buffer[streamOffsetList[j].offset])!=0)||((recompBuffer[1]-buffer[(1+streamOffsetList[j].offset)])!=0))){
                                    fullmatch=true;
                                    streamOffsetList[j].identBytes=identicalBytes;
                                    streamOffsetList[j].clevel=clevel;
                                    streamOffsetList[j].memlvl=memlevel;
                                    streamOffsetList[j].window=window;
                                    streamOffsetList[j].diffByteOffsets.clear();
                                    streamOffsetList[j].diffByteVal.clear();
                                    if (recompBuffer[0]!=buffer[streamOffsetList[j].offset]){
                                        streamOffsetList[j].firstDiffByte=0;
                                        streamOffsetList[j].diffByteVal.push_back(buffer[streamOffsetList[j].offset]);
                                    } else {
                                        streamOffsetList[j].firstDiffByte=1;
                                        streamOffsetList[j].diffByteVal.push_back(buffer[(1+streamOffsetList[j].offset)]);
                                    }
                                    streamOffsetList[j].diffByteOffsets.push_back(0);
                                }
                                if ((identicalBytes>streamOffsetList[j].identBytes)&&!fullmatch){
                                    streamOffsetList[j].identBytes=identicalBytes;
                                    streamOffsetList[j].clevel=clevel;
                                    streamOffsetList[j].memlvl=memlevel;
                                    streamOffsetList[j].window=window;
                                    streamOffsetList[j].firstDiffByte=-1;
                                    streamOffsetList[j].diffByteOffsets.clear();
                                    streamOffsetList[j].diffByteVal.clear();
                                    int_fast64_t last_i=0;
                                    for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                                        if (recompBuffer[i]!=buffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                            if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                streamOffsetList[j].firstDiffByte=(i);
                                                streamOffsetList[j].diffByteOffsets.push_back(0);
                                                streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                last_i=i;
                                            } else {
                                                streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                streamOffsetList[j].diffByteVal.push_back(buffer[(i+streamOffsetList[j].offset)]);
                                                last_i=i;
                                            }
                                        }
                                    }
                                }
                                clevel--;
                            }
                        }

                        //deallocate the Zlib stream and check if it went well
                        ret=deflateEnd(&strm1);
                        /*
                        if (ret != Z_OK)
                        {
                            std::cout<<std::endl<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
                            abort();
                        }*/
                        delete [] recompBuffer;
                    } while ((!fullmatch)&&(clevel>=1));
                    memlevel--;
                } while ((!fullmatch)&&(memlevel>=1));

                break;
            }
            case Z_DATA_ERROR: //the compressed data was invalid, this should never happen since the offsets have been checked
            {
                std::cout<<std::endl<<"inflate() failed with data error"<<std::endl;
                abort();
            }
            case Z_BUF_ERROR: //this should not happen since the decompressed lengths are known
            {
                std::cout<<std::endl<<"inflate() failed with memory error"<<std::endl;
                abort();
            }
            default: //shit hit the fan, should never happen normally
            {
                std::cout<<std::endl<<"inflate() failed with exit code:"<<ret<<std::endl;
                abort();
            }
        }
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        ret=inflateEnd(&strm);
        if (ret!=Z_OK)
        {
            std::cout<<std::endl<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        delete [] decompBuffer;
        
        end = std::clock();
        double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
        std::cout << " - time: " << elapsed_secs << " s.";
        std::cout << " > " << streamOffsetList[j].identBytes
                  << " |"<<(int)streamOffsetList[j].memlvl<<"|"<<(int)streamOffsetList[j].clevel<<"|"
                  << (int)streamOffsetList[j].window<<"|";
        
    }
    std::cout<<std::endl;
    uint64_t recomp=0;
    for (j=0; j<streamOffsetList.size(); j++){
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            recomp++;
            streamOffsetList[j].recomp=true;
        }
    }
    std::cout<<std::endl<<"Recompressed "<<recomp<<" / "<<streamOffsetList.size()<<std::endl;

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
    outfile.write(reinterpret_cast<char*>(&recomp), 8);//number of recompressed streams

    for(j=0;j<streamOffsetList.size();j++){//write recompressed stream descriptions
        if (streamOffsetList[j].recomp==true){//we are operating on the j-th stream
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].offset), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].streamLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].inflatedLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].clevel), 1);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].window), 1);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].memlvl), 1);

            {
                uint64_t diffbytes=streamOffsetList[j].diffByteOffsets.size();
                outfile.write(reinterpret_cast<char*>(&diffbytes), 8);
                if (diffbytes>0){
                    uint64_t firstdiff=streamOffsetList[j].firstDiffByte;
                    outfile.write(reinterpret_cast<char*>(&firstdiff), 8);
                    uint64_t diffos;
                    uint8_t diffval;
                    for(i=0;i<diffbytes;i++){
                        diffos=streamOffsetList[j].diffByteOffsets[i];
                        outfile.write(reinterpret_cast<char*>(&diffos), 8);
                    }
                    for(i=0;i<diffbytes;i++){
                        diffval=streamOffsetList[j].diffByteVal[i];
                        outfile.write(reinterpret_cast<char*>(&diffval), 1);
                    }
                }
            }

            //create a new Zlib stream to do decompression
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in= streamOffsetList[j].streamLength;
            strm.next_in=buffer+streamOffsetList[j].offset;
            //initialize the stream for decompression and check for error
            ret=inflateInit(&strm);
            if (ret != Z_OK)
            {
                std::cout<<"inflateInit() failed with exit code:"<<ret<<std::endl;
                abort();
            }
            //a buffer needs to be created to hold the resulting decompressed data
            unsigned char* decompBuffer= new unsigned char[streamOffsetList[j].inflatedLength];
            strm.next_out=decompBuffer;
            strm.avail_out=streamOffsetList[j].inflatedLength;
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
            outfile.write(reinterpret_cast<char*>(decompBuffer), streamOffsetList[j].inflatedLength);
            delete [] decompBuffer;
        }
    }


    uint64_t lastos=0, lastlen=0;
    for(j=0;j<streamOffsetList.size();j++){//write the gaps before streams and non-recompressed streams to disk as the residue
        if ((lastos+lastlen)==streamOffsetList[j].offset){
            if (streamOffsetList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(buffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }else{
            outfile.write(reinterpret_cast<char*>(buffer+lastos+lastlen), (streamOffsetList[j].offset-(lastos+lastlen)));
            if (streamOffsetList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(buffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }
        lastos=streamOffsetList[j].offset;
        lastlen=streamOffsetList[j].streamLength;
    }
    if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
        outfile.write(reinterpret_cast<char*>(buffer+lastos+lastlen), (infileSize-(lastos+lastlen)));
    }

    atzlen=outfile.tellp();
    std::cout<<"Total bytes written: "<<atzlen<<std::endl;
    outfile.seekp(4);//go back to the placeholder
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    streamOffsetList.clear();
    streamOffsetList.shrink_to_fit();
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
        std::vector<streamOffset> streamOffsetList;
        streamOffsetList.reserve(nstrms);
        //reead in all the info about the streams
        for (j=0;j<nstrms;j++){
            streamOffsetList.push_back(streamOffset(*reinterpret_cast<uint64_t*>(&buffer[lastos]), -1, *reinterpret_cast<uint64_t*>(&buffer[8+lastos]), *reinterpret_cast<uint64_t*>(&buffer[16+lastos])));
            streamOffsetList[j].clevel=buffer[24+lastos];
            streamOffsetList[j].window=buffer[25+lastos];
            streamOffsetList[j].memlvl=buffer[26+lastos];
            //partial match handling
            uint64_t diffbytes=*reinterpret_cast<uint64_t*>(&buffer[27+lastos]);
            if (diffbytes>0){//if the stream is just a partial match
                streamOffsetList[j].firstDiffByte=*reinterpret_cast<uint64_t*>(&buffer[35+lastos]);
                streamOffsetList[j].diffByteOffsets.reserve(diffbytes);
                streamOffsetList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamOffsetList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&buffer[43+8*i+lastos]));
                    streamOffsetList[j].diffByteVal.push_back(buffer[43+diffbytes*8+i+lastos]);
                }
                streamOffsetList[j].atzInfos=&buffer[43+diffbytes*9+lastos];
                lastos=lastos+43+diffbytes*9+streamOffsetList[j].inflatedLength;
            } else{//if the stream is a full match
                streamOffsetList[j].firstDiffByte=-1;//negative value signals full match
                streamOffsetList[j].atzInfos=&buffer[35+lastos];
                lastos=lastos+35+streamOffsetList[j].inflatedLength;
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
        for(j=0;j<streamOffsetList.size();j++){
            if ((lastos+lastlen)==streamOffsetList[j].offset){//no gap before the stream
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+32768];
                {
                    //do compression
                    z_stream strm;
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    int ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamOffsetList[j].streamLength+32768;
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
                if (streamOffsetList[j].firstDiffByte>=0){
                    uint64_t db=streamOffsetList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamOffsetList[j].firstDiffByte+streamOffsetList[j].diffByteOffsets[i]+sum]=streamOffsetList[j].diffByteVal[i];
                        sum=sum+streamOffsetList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                delete [] compBuffer;
            }else{
                recfile.write(reinterpret_cast<char*>(buffer+residueos+gapsum), (streamOffsetList[j].offset-(lastos+lastlen)));
                gapsum=gapsum+(streamOffsetList[j].offset-(lastos+lastlen));
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+32768];
                {
                    //do compression
                    z_stream strm;
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    int ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamOffsetList[j].streamLength+32768;
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
                if (streamOffsetList[j].firstDiffByte>=0){
                    uint64_t db=streamOffsetList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamOffsetList[j].firstDiffByte+streamOffsetList[j].diffByteOffsets[i]+sum]=streamOffsetList[j].diffByteVal[i];
                        sum=sum+streamOffsetList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                delete [] compBuffer;
            }
            lastos=streamOffsetList[j].offset;
            lastlen=streamOffsetList[j].streamLength;
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