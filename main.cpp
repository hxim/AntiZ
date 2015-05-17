#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>
#include <iomanip>

class streamOffset{
public:
    streamOffset(){
        offset=-1;
    }
    streamOffset(uint64_t os, uint64_t sl, uint64_t il){
        offset=os;
        streamLength=sl;
        inflatedLength=il;
        clevel=9;
        window=15;
        memlvl=9;
        identBytes=0;
        firstDiffByte=-1;
        recomp=false;
        atzInfos=0;
    }
    ~streamOffset(){
        diffByteOffsets.clear();
        diffByteOffsets.shrink_to_fit();
        diffByteVal.clear();
        diffByteVal.shrink_to_fit();
    }
    uint64_t offset;
    uint64_t streamLength;
    uint64_t inflatedLength;
    uint8_t clevel;
    uint8_t window;
    uint8_t memlvl;
    int_fast64_t identBytes;
    int_fast64_t firstDiffByte;//the offset of the first byte that does not match, relative to stream start, not file start
    std::vector<int_fast64_t> diffByteOffsets;//offsets of bytes that differ, this is an incremental offset list to enhance recompression, kinda like a PNG filter
    //this improves compression if the mismatching bytes are consecutive, eg. 451,452,453,...(no repetitions, hard to compress)
    //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
    std::vector<unsigned char> diffByteVal;
    bool recomp;
    unsigned char* atzInfos;
};


bool CheckOffset(int_fast64_t i, unsigned char *next_in, uint64_t avail_in, streamOffset &s){
    z_stream strm;
    strm.zalloc=Z_NULL;
    strm.zfree=Z_NULL;
    strm.opaque=Z_NULL;
    strm.avail_in=avail_in;
    strm.next_in=next_in;

    //initialize the stream for decompression and check for error
    if (inflateInit(&strm)!=Z_OK) return false;

    bool success=false;
    int_fast64_t memScale=1;
    while (true){
        //a buffer needs to be created to hold the resulting decompressed data
        //this is a big problem since the zlib header does not contain the length of the decompressed data
        //the best we can do is to take a guess, and see if it was big enough, if not then scale it up
        unsigned char* decompBuffer= new unsigned char[(memScale*5*avail_in)]; //just a wild guess, corresponds to a compression ratio of 20%
        strm.next_out=decompBuffer;
        strm.avail_out=memScale*5*avail_in;
        int ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        if (ret==Z_STREAM_END && strm.total_in>=16)//decompression was succesful
        {
            s=streamOffset(i, strm.total_in, strm.total_out);
            success=true;
        }
        if (inflateEnd(&strm)!=Z_OK) success=false;
        delete [] decompBuffer;
        if (ret!=Z_BUF_ERROR) break;
        memScale++;//increase buffer size for the next iteration
    };
    return success;
}

int main(int argc, char* argv[]) {
    using std::cout;
    using std::endl;
    using std::cin;
    using std::vector;
    uint64_t lastos=0;
    uint64_t lastlen=0;
    uint64_t atzlen=0;//placeholder for the length of the atz file
    std::ofstream outfile;
    int_fast64_t numGoodOffsets;
    int_fast64_t identicalBytes;
    int_fast64_t j=0;
    int memlevel=9;
    int clevel=9;
    int window=15;
    bool fullmatch=false;
    z_stream strm1;
    uint64_t recomp=0;

    int recompTresh=128;//streams are only recompressed if the best match differs from the original in <= recompTresh bytes
    int sizediffTresh=128;//streams are only compared when the size difference is <= sizediffTresh
    //DO NOT turn off slowmode, the alternative code (optimized mode) does not work at all
    bool slowmode=true;//slowmode bruteforces the zlib parameters, optimized mode only tries probable parameters based on the 2-byte header
    int_fast64_t concentrate=-404;//only try to recompress the stream# givel here, -1 disables this and runs on all streams

    int_fast64_t numOffsets=0;
    int ret=-9;
    vector<streamOffset> streamOffsetList;
    z_stream strm;

    int_fast64_t i;
    unsigned char* rBuffer;
    std::ifstream infile;

    //PHASE 0
    //opening file
    uint64_t infileSize;
    char* infile_name;
    char* reconfile_name;
    char* atzfile_name;
    if (argc>=2){// if we get at least one string use it as input file name
        cout<<"Input file: "<<argv[1]<<endl;
        if (argc>=3 && strcmp(argv[2], "-r")==0){//if we get at least two strings use the second as a parameter
            //if we get -r, treat the file as an ATZ file and skip to reconstruction
            atzfile_name=argv[1];

            reconfile_name= new char[strlen(argv[1])+5];
            memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
            strcpy(reconfile_name, argv[1]);
            reconfile_name=strcat(reconfile_name, ".rec");
            cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<endl;
            cout<<"overwriting "<<reconfile_name<<" if present"<<endl;
            goto PHASE5;
        }else{//if we get only the filename go forward to creating an ATZ file from it
            infile_name=argv[1];
            atzfile_name= new char[strlen(argv[1])+5];
            memset(atzfile_name, 0, (strlen(argv[1])+5));//null out the entire string
            strcpy(atzfile_name, argv[1]);
            atzfile_name=strcat(atzfile_name, ".atz");

            reconfile_name= new char[strlen(argv[1])+5];
            memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
            strcpy(reconfile_name, argv[1]);
            reconfile_name=strcat(reconfile_name, ".rec");
            cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<endl;
        }
    }else{//if we get nothing from the CLI
        cout<<"error: no input specified"<<endl;
        abort();
    }

    infile.open(infile_name, std::ios::in | std::ios::binary);
    if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
        abort();
    }
    //getting the size of the file
    infile.seekg (0, infile.end);
    infileSize=infile.tellg();
    infile.seekg (0, infile.beg);

    //setting up read buffer and reading the entire file into the buffer
    rBuffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
    infile.close();

    //PHASE 1+2
    //search the file for zlib headers and try to decompress

    cout<<endl;
    for(i=0;i<infileSize-1;i++){
        //search for 7801, 785E, 789C, 78DA, 68DE, 6881, 6843, 6805, 58C3, 5885, 5847, 5809,
        //           48C7, 4889, 484B, 480D, 38CB, 388D, 384F, 3811, 28CF, 2891, 2853, 2815
        int hbits = rBuffer[i]>>4;
        int lbits = rBuffer[i]&15;
        if ((lbits==8)&&(hbits>=2)&&(hbits<=7)){
            int v = rBuffer[i+1];
            v=(v&(255-32-1))+((v&32)?1:0)+(v&1)*32;//swap 1st and 5th bit
            if ((v+hbits*4)%62==60){
                #ifdef debug
                cout<<"Found zlib header("<<std::hex<<std::setfill('0')<<std::uppercase<<std::setw(2)<<(int)rBuffer[i]
                <<" "<<std::setw(2)<<(int)rBuffer[i+1]<<std::dec<<") with "<<(1<<(hbits-2))<<"K window at offset: "<<i<<endl;
                #endif // debug
                streamOffset s;
                if (CheckOffset(i, &rBuffer[i], infileSize-i, s)){
                    streamOffsetList.push_back(s);
                    i+=s.streamLength-1;
                }
            }
        }
    }
    cout<<"Good offsets: "<<streamOffsetList.size()<<endl;

    //PHASE 3
    //start trying to find the parameters to use for recompression

    numGoodOffsets=streamOffsetList.size();
    cout<<endl;
    for (j=0; j<numGoodOffsets; j++){
        if ((concentrate>=0)&&(j==0)) {
            j=concentrate;
            numGoodOffsets=concentrate;
        }
        //reset the Zlib stream to do decompression
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        fullmatch=false;
        memlevel=9;
        window=15;
        //the lengths of the zlib streams have been saved by the previous phase
        strm.avail_in = streamOffsetList[j].streamLength;
        strm.next_in=rBuffer+streamOffsetList[j].offset;//this is effectively adding an integer to a pointer, resulting in a pointer
        //initialize the stream for decompression and check for error
        ret=inflateInit(&strm);
        if (ret != Z_OK)
        {
            cout<<"inflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
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
                if (slowmode){
                    do{
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
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    abort();
                                }

                                //prepare for compressing in one pass
                                strm1.avail_in=streamOffsetList[j].inflatedLength;
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                strm1.next_out=recompBuffer;
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                                //check the return value to see if everything went well
                                if (ret != Z_STREAM_END){
                                    cout<<"recompression failed with exit code:"<<ret<<endl;
                                    abort();
                                }

                                //test if the recompressed stream matches the input data
                                if (strm1.total_out!=streamOffsetList[j].streamLength){
                                    identicalBytes=0;
                                    //cout<<"   size difference: "<<(strm1.total_out-static_cast<int64_t>(streamOffsetList[j].streamLength))<<endl;
                                    if (abs((strm1.total_out-streamOffsetList[j].streamLength))>sizediffTresh){
                                    } else {
                                        if (strm1.total_out<streamOffsetList[j].streamLength){
                                            for (i=0; i<strm1.total_out;i++){
                                                if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
                                                    identicalBytes++;
                                                }
                                            }
                                        } else {
                                            for (i=0; i<streamOffsetList[j].streamLength;i++){
                                                if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
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
                                                    if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                        if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                            streamOffsetList[j].firstDiffByte=(i);
                                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            last_i=i;
                                                        } else {
                                                            streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            //cout<<"   different byte:"<<i<<endl;
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
                                                    streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+strm1.total_out+streamOffsetList[j].offset)]);
                                                }
                                            } else {//the recompressed stream is longer than the original
                                                for (i=0; i<streamOffsetList[j].streamLength;i++){
                                                    if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                        if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                            streamOffsetList[j].firstDiffByte=(i);
                                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            last_i=i;
                                                        } else {
                                                            streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            //cout<<"   different byte:"<<i<<endl;
                                                            last_i=i;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    clevel--;
                                } else {
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
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
                                        if (((streamOffsetList[j].streamLength-identicalBytes)==2)&&((recompBuffer[0]-rBuffer[streamOffsetList[j].offset])!=0)&&((recompBuffer[1]-rBuffer[(1+streamOffsetList[j].offset)])!=0)){
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
                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[streamOffsetList[j].offset]);
                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(1+streamOffsetList[j].offset)]);
                                        }
                                        if (((streamOffsetList[j].streamLength-identicalBytes)==1)&&(((recompBuffer[0]-rBuffer[streamOffsetList[j].offset])!=0)||((recompBuffer[1]-rBuffer[(1+streamOffsetList[j].offset)])!=0))){
                                            fullmatch=true;
                                            streamOffsetList[j].identBytes=identicalBytes;
                                            streamOffsetList[j].clevel=clevel;
                                            streamOffsetList[j].memlvl=memlevel;
                                            streamOffsetList[j].window=window;
                                            streamOffsetList[j].diffByteOffsets.clear();
                                            streamOffsetList[j].diffByteVal.clear();
                                            if (recompBuffer[0]!=rBuffer[streamOffsetList[j].offset]){
                                                streamOffsetList[j].firstDiffByte=0;
                                                streamOffsetList[j].diffByteVal.push_back(rBuffer[streamOffsetList[j].offset]);
                                            } else {
                                                streamOffsetList[j].firstDiffByte=1;
                                                streamOffsetList[j].diffByteVal.push_back(rBuffer[(1+streamOffsetList[j].offset)]);
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
                                                if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                    if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                        streamOffsetList[j].firstDiffByte=(i);
                                                        streamOffsetList[j].diffByteOffsets.push_back(0);
                                                        streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                        last_i=i;
                                                    } else {
                                                        streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                        streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
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
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    abort();
                                }
                                delete [] recompBuffer;
                            } while ((!fullmatch)&&(clevel>=1));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        window--;
                    } while ((!fullmatch)&&(window>=10));
                }
                break;
            }
            case Z_DATA_ERROR: //the compressed data was invalid, this should never happen since the offsets have been checked
            {
                cout<<"inflate() failed with data error"<<endl;
                abort();
            }
            case Z_BUF_ERROR: //this should not happen since the decompressed lengths are known
            {
                cout<<"inflate() failed with memory error"<<endl;
                abort();
            }
            default: //shit hit the fan, should never happen normally
            {
                cout<<"inflate() failed with exit code:"<<ret<<endl;
                abort();
            }
        }
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        ret=inflateEnd(&strm);
        if (ret!=Z_OK)
        {
            cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
            abort();
        }
        delete [] decompBuffer;
    }
    if (concentrate>=0){
        numGoodOffsets=streamOffsetList.size();
    }
    cout<<endl;
    for (j=0; j<streamOffsetList.size(); j++){
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            recomp++;
            streamOffsetList[j].recomp=true;
        }
    }
    cout<<"recompressed:"<<recomp<<"/"<<streamOffsetList.size()<<endl;

    //PHASE 4
    //take the information created in phase 3 and use it to create an ATZ file(see ATZ file format spec.)
    outfile.open(atzfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
       cout << "error: open file for output failed!" << endl;
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
            strm.next_in=rBuffer+streamOffsetList[j].offset;
            //initialize the stream for decompression and check for error
            ret=inflateInit(&strm);
            if (ret != Z_OK)
            {
                cout<<"inflateInit() failed with exit code:"<<ret<<endl;
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
                    cout<<"inflate() failed with exit code:"<<ret<<endl;
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                return ret;
            }
            outfile.write(reinterpret_cast<char*>(decompBuffer), streamOffsetList[j].inflatedLength);
            delete [] decompBuffer;
        }
    }


    for(j=0;j<streamOffsetList.size();j++){//write the gaps before streams and non-recompressed streams to disk as the residue
        if ((lastos+lastlen)==streamOffsetList[j].offset){
            if (streamOffsetList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }else{
            outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (streamOffsetList[j].offset-(lastos+lastlen)));
            if (streamOffsetList[j].recomp==false){
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }
        lastos=streamOffsetList[j].offset;
        lastlen=streamOffsetList[j].streamLength;
    }
    if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
        outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (infileSize-(lastos+lastlen)));
    }

    atzlen=outfile.tellp();
    cout<<"Total bytes written: "<<atzlen<<endl;
    outfile.seekp(4);//go back to the placeholder
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    streamOffsetList.clear();
    streamOffsetList.shrink_to_fit();
    outfile.close();
    delete [] rBuffer;

    PHASE5:
    //PHASE 5: verify that we can reconstruct the original file, using only data from the ATZ file
    infileSize=0;
    atzlen=0;
    lastos=28;
    uint64_t origlen=0;
    uint64_t nstrms=0;

    std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary);
    if (!atzfile.is_open()) {
       cout << "error: open ATZ file for input failed!" << endl;
        abort();
    }
    cout<<"reconstructing from "<<atzfile_name<<endl;
    atzfile.seekg (0, atzfile.end);
    infileSize=atzfile.tellg();
    atzfile.seekg (0, atzfile.beg);
    cout<<"File size:"<<infileSize<<endl;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* atzBuffer = new unsigned char[infileSize];
    atzfile.read(reinterpret_cast<char*>(atzBuffer), infileSize);
    atzfile.close();

    if ((atzBuffer[0]!=65)||(atzBuffer[1]!=84)||(atzBuffer[2]!=90)||(atzBuffer[3]!=1)){
        cout<<"ATZ1 header not found"<<endl;
        abort();
    }
    atzlen=*reinterpret_cast<uint64_t*>(&atzBuffer[4]);
    if (atzlen!=infileSize){
        cout<<"atzlen mismatch"<<endl;
        abort();
    }
    origlen=*reinterpret_cast<uint64_t*>(&atzBuffer[12]);
    nstrms=*reinterpret_cast<uint64_t*>(&atzBuffer[20]);
    if (nstrms>0){
        streamOffsetList.reserve(nstrms);
        //reead in all the info about the streams
        for (j=0;j<nstrms;j++){
            streamOffsetList.push_back(streamOffset(*reinterpret_cast<uint64_t*>(&atzBuffer[lastos]), *reinterpret_cast<uint64_t*>(&atzBuffer[8+lastos]), *reinterpret_cast<uint64_t*>(&atzBuffer[16+lastos])));
            streamOffsetList[j].clevel=atzBuffer[24+lastos];
            streamOffsetList[j].window=atzBuffer[25+lastos];
            streamOffsetList[j].memlvl=atzBuffer[26+lastos];
            //partial match handling
            uint64_t diffbytes=*reinterpret_cast<uint64_t*>(&atzBuffer[27+lastos]);
            if (diffbytes>0){//if the stream is just a partial match
                streamOffsetList[j].firstDiffByte=*reinterpret_cast<uint64_t*>(&atzBuffer[35+lastos]);
                streamOffsetList[j].diffByteOffsets.reserve(diffbytes);
                streamOffsetList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamOffsetList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&atzBuffer[43+8*i+lastos]));
                    streamOffsetList[j].diffByteVal.push_back(atzBuffer[43+diffbytes*8+i+lastos]);
                }
                streamOffsetList[j].atzInfos=&atzBuffer[43+diffbytes*9+lastos];
                lastos=lastos+43+diffbytes*9+streamOffsetList[j].inflatedLength;
            } else{//if the stream is a full match
                streamOffsetList[j].firstDiffByte=-1;//negative value signals full match
                streamOffsetList[j].atzInfos=&atzBuffer[35+lastos];
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
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
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
                            cout<<"deflate() failed with exit code:"<<ret<<endl;
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
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
                recfile.write(reinterpret_cast<char*>(atzBuffer+residueos+gapsum), (streamOffsetList[j].offset-(lastos+lastlen)));
                gapsum=gapsum+(streamOffsetList[j].offset-(lastos+lastlen));
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+32768];
                {
                    //do compression
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
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
                            cout<<"deflate() failed with exit code:"<<ret<<endl;
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
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
            recfile.write(reinterpret_cast<char*>(atzBuffer+residueos+gapsum), (origlen-(lastos+lastlen)));
        }
        recfile.close();
    }else{//if there are no recompressed streams
        std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
        recfile.write(reinterpret_cast<char*>(atzBuffer+28), origlen);
        recfile.close();
    }

    delete [] atzBuffer;
    return 0;
}
