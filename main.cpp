#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>

#define filename "test.bin"
#define filename_out "atztest.atz"
#define reconfile "recon.bin"

void pause(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}

class fileOffset{
public:
    fileOffset(){
        offset=0;
        offsetType=1;
        abort();//the default constructor should not be used in this version
    }
    fileOffset(int_fast64_t os, int ot){
        offset=os;
        offsetType=ot;
    }
    int_fast64_t offset;
    int offsetType;
    //int_fast64_t offset;
    //int_fast64_t offset;
};

class streamOffset{
public:
    streamOffset(){
        offset=0;
        offsetType=1;
        streamLength=1;
        inflatedLength=1;
        abort();//the default constructor should not be used in this version
    }
    streamOffset(uint64_t os, int ot, uint64_t sl, uint64_t il){
        offset=os;
        offsetType=ot;
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
    int offsetType;
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

int main() {
	using std::cout;
	using std::endl;
	using std::cin;
	using std::vector;
	//PHASE 0
	//opening file
	/*
        objects created:
            infile: input file stream
            statresults: the results of file statistics
            rBuffer: to hold the input file in memory
        objects destroyed:
            infile: destroyed with infile.close()
        objects created, but not provided or destroyed:
            statresults
        variables declared:
            infileSize: to hold the size of input file for later use without needing to use the statresults struct
        debug variables declared:
            none
        provides:
            infileSize
            rBuffer
        requires:
            none
	*/
	uint64_t infileSize;
	std::ifstream infile(filename, std::ios::in | std::ios::binary);
	if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
       pause();
 	   abort();
	}
	//getting the size of the file
	struct stat statresults;
    if (stat(filename, &statresults) == 0){
    	cout<<"File size:"<<statresults.st_size<<endl;
    }
    else{
    	cout<<"Error determining file size."<<endl;
    	pause();
    	abort();
    }
    infileSize=statresults.st_size;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* rBuffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
    infile.close();

    //PHASE 1
	//search the file for zlib headers, count them and create an offset list
	#ifdef debug
	uint_fast64_t nMatch1=0;
	uint_fast64_t nMatch2=0;
	uint_fast64_t nMatch3=0;
	uint_fast64_t nMatch4=0;
	uint_fast64_t nMatch5=0;
	uint_fast64_t nMatch6=0;
	#endif
	//offsetList stores memory offsets where potential headers can be found, and the type of the offset
	vector<fileOffset> offsetList;
	int_fast64_t i;
	//try to guess the number of potential zlib headers in the file from the file size
	//this value is purely empirical, may need tweaking
	offsetList.reserve(static_cast<int_fast64_t>(infileSize/1912));
	#ifdef debug
	cout<<"Offset list initial capacity:"<<offsetList.capacity()<<endl;
	pause();
	#endif
	cout<<endl;
	for(i=0;i<infileSize;i++){
        switch(rBuffer[i]){
            case 120://hex 78
            {
                switch(rBuffer[i+1]){
                    case 1:{//hex 78 01
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 01) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 1));
                        break;
                    }
                    case 94:{//hex 78 5E
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 5E) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 2));
                        break;
                    }
                    case 156:{//hex 78 9C
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 9C) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 3));
                        break;
                    }
                    case 218:{//hex 78 DA
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 DA) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 4));
                        break;
                    }
                }
                break;
            }
            case 104://hex 68
            {
                switch(rBuffer[i+1]){
                    case 222:{//hex 68 DE
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 DE) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 5));
                        break;
                    }
                    case 129:{//hex 68 81
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 81) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 6));
                        break;
                    }
                    case 67:{//hex 68 43
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 43) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 7));
                        break;
                    }
                    case 5:{//hex 68 05
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 05) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 8));
                        break;
                    }
                }
                break;
            }
            case 88://hex 58
            {
                switch(rBuffer[i+1]){
                    case 195:{//hex 58 C3
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 C3) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 9));
                        break;
                    }
                    case 133:{//hex 58 85
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 85) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 10));
                        break;
                    }
                    case 71:{//hex 58 47
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 47) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 11));
                        break;
                    }
                    case 9:{//hex 58 09
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 09) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 12));
                        break;
                    }
                }
                break;
            }
            case 72://hex 48
            {
                switch(rBuffer[i+1]){
                    case 199:{//hex 48 C7
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 C7) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 13));
                        break;
                    }
                    case 137:{//hex 48 89
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 89) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 14));
                        break;
                    }
                    case 75:{//hex 48 4B
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 4B) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 15));;
                        break;
                    }
                    case 13:{//hex 48 0D
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 0D) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 16));
                        break;
                    }
                }
                break;
            }
            case 56://hex 38
            {
                switch(rBuffer[i+1]){
                    case 203:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 CB) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 17));
                        break;
                    }
                    case 141:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 8D) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 18));
                        break;
                    }
                    case 79:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 4F) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 19));
                        break;
                    }
                    case 17:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 11) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 20));
                        break;
                    }
                }
                break;
            }
            case 40://hex 28
            {
                switch(rBuffer[i+1]){
                    case 207:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 CF) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 21));
                        break;
                    }
                    case 145:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 91) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 22));
                        break;
                    }
                    case 83:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 53) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 23));
                        break;
                    }
                    case 21:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 15) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(fileOffset(i, 24));
                        break;
                    }
                }
                break;
            }
        }
    }
    #ifdef debug
    cout<<endl;
	cout<<"32K full header matches:"<<nMatch1<<endl;
	cout<<"16K full header matches:"<<nMatch2<<endl;
	cout<<"8K full header matches:"<<nMatch3<<endl;
	cout<<"4K full header matches:"<<nMatch4<<endl;
	cout<<"2K full header matches:"<<nMatch5<<endl;
	cout<<"1K full header matches:"<<nMatch6<<endl;
	cout<<"Total header matches:"<<(nMatch1+nMatch2+nMatch3+nMatch4+nMatch5+nMatch6)<<endl;
	cout<<"Number of collected offsets:"<<offsetList.size()<<endl;
	//sanity check, the number of offsets in the vector should always be the sum of found offsets
	if ((nMatch1+nMatch2+nMatch3+nMatch4+nMatch5+nMatch6)!=offsetList.size()){
        cout<<"search error"<<endl;
        pause();
        abort();
	}
    pause();
    #endif // debug

    //PHASE 2
    //start trying to decompress at the collected offsets
    /*
		objects created:
			streamOffsetList: vector holding offsets proven to be good
			streamType: vector holding the types of proven offsets. Index-coupled to streamOffsetList.
			streamLength: vector holding the lengths of proven offsets. Index-coupled to streamOffsetList.
			inflatedLength: vector holding the inflated lengths of proven offsets. Index-coupled to streamOffsetList.
			strm: zlib stream holding decompressed data
		objects destroyed:
            none
		objects created, but not provided or destroyed:
            none
		variables declared:
			lastGoodOffset: while iterating through the potential offsets, this keeps track of the previous good offset. Used for skipping offsets.
			lastStreamLength: while iterating through the potential offsets, this keeps track of the length of the previous good stream. Used for skipping offsets.
			memScale: used for dynamically scaling the decompression buffer if it proves to be too small
			j: another general purpose iterator, to be used in nested for loops etc.
			numOffsets: used to store the number of potential offsets. This is used to eliminate the need for a function call every time the loop need this number. Should improve speed slightly.
			ret: used to store the return values of zlib functions
		debug variables declared:
			dataErrors:the number of improper offsets, that are not really zlib streams
			numDecomp32k..1k: number of offsets with 32k..1k window that are valid
		required:
            offsetList
            offsetType
            infileSize
            rBuffer
		provides:
			streamOffsetList
			streamType
			streamLength
			inflatedLength
			j
			strm
			ret
	*/
    int_fast64_t lastGoodOffset=0;
    int_fast64_t lastStreamLength=0;
    int_fast64_t memScale=1;
	int_fast64_t numOffsets=0;
	int ret=-9;
	vector<streamOffset> streamOffsetList;
	z_stream strm;
    #ifdef debug
	int_fast64_t dataErrors=0;
    int_fast64_t numDecomp32k=0;
    int_fast64_t numDecomp16k=0;
    int_fast64_t numDecomp8k=0;
    int_fast64_t numDecomp4k=0;
    int_fast64_t numDecomp2k=0;
    int_fast64_t numDecomp1k=0;
    uint_fast64_t type1=0;
    uint_fast64_t type2=0;
    uint_fast64_t type3=0;
    uint_fast64_t type4=0;
    #endif // debug

    numOffsets=offsetList.size();
    for (i=0; i<numOffsets; i++)
    {
        if ((lastGoodOffset+lastStreamLength)<=offsetList[i].offset)
        {
            /*
            local variables and objects:
                decompBuffer: used to hold the data resulting from the decompression
            */
            //create a new Zlib stream to do decompression
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            //since we have no idea about the lenght of the zlib stream, take the worst case, i.e. everything after the header belongs to the stream
            strm.avail_in= infileSize-offsetList[i].offset;
            strm.next_in=rBuffer+offsetList[i].offset;//this is effectively adding an integer to a pointer, resulting in a pointer
            //initialize the stream for decompression and check for error
            ret=inflateInit(&strm);
            if (ret != Z_OK)
            {
                cout<<"inflateInit() failed with exit code:"<<ret<<endl;
                pause();
                abort();
            }
            //a buffer needs to be created to hold the resulting decompressed data
            //this is a big problem since the zlib header does not contain the length of the decompressed data
            //the best we can do is to take a guess, and see if it was big enough, if not then scale it up
            unsigned char* decompBuffer= new unsigned char[(memScale*5*(infileSize-offsetList[i].offset))]; //just a wild guess, corresponds to a compression ratio of 20%
            strm.next_out=decompBuffer;
            strm.avail_out=memScale*5*(infileSize-offsetList[i].offset);
            ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
            //check the return value
            switch (ret)
            {
                case Z_DATA_ERROR://the compressed data was invalid, most likely it was not a good offset
                {
                    #ifdef debug
                    dataErrors++;
                    #endif // debug
                    break;
                }
                case Z_STREAM_END://decompression was succesful
                {
                    #ifdef debug
                    switch(offsetList[i].offsetType){
                        //32K window streams
                        case 1:{//78 01
                            cout<<"Stream #"<<i<<"(78 01) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type1++;
                            break;
                        }
                        case 2:{//78 5E
                            cout<<"Stream #"<<i<<"(78 5E) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type2++;
                            break;
                        }
                        case 3:{//78 9C
                            cout<<"Stream #"<<i<<"(78 9C) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type3++;
                            break;
                        }
                        case 4:{//78 DA
                            cout<<"Stream #"<<i<<"(78 DA) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type4++;
                            break;
                        }
                        //16K window streams
                        case 5:{//68 DE
                            cout<<"Stream #"<<i<<"(68 DE) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 6:{//68 81
                            cout<<"Stream #"<<i<<"(68 81) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 7:{//68 43
                            cout<<"Stream #"<<i<<"(68 43) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 8:{//68 05
                            cout<<"Stream #"<<i<<"(68 05) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        //8K window streams
                        case 9:{//58 C3
                            cout<<"Stream #"<<i<<"(58 C3) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 10:{//58 85
                            cout<<"Stream #"<<i<<"(58 85) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 11:{//58 47
                            cout<<"Stream #"<<i<<"(58 47) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 12:{//58 09
                            cout<<"Stream #"<<i<<"(58 09) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        //4K window streams
                        case 13:{
                            cout<<"Stream #"<<i<<"(48 C7) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 14:{
                            cout<<"Stream #"<<i<<"(48 89) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 15:{
                            cout<<"Stream #"<<i<<"(48 4B) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 16:{
                            cout<<"Stream #"<<i<<"(48 0D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        //2K window streams
                        case 17:{
                            cout<<"Stream #"<<i<<"(38 CB) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 18:{
                            cout<<"Stream #"<<i<<"(38 8D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 19:{
                            cout<<"Stream #"<<i<<"(38 4F) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 20:{
                            cout<<"Stream #"<<i<<"(38 11) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        //1K window streams
                        case 21:{
                            cout<<"Stream #"<<i<<"(28 CF) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 22:{
                            cout<<"Stream #"<<i<<"(28 91) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 23:{
                            cout<<"Stream #"<<i<<"(28 53) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 24:{
                            cout<<"Stream #"<<i<<"(28 15) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                    }
                    #endif // debug
                    lastGoodOffset=offsetList[i].offset;
                    lastStreamLength=strm.total_in;
                    streamOffsetList.push_back(streamOffset(offsetList[i].offset, offsetList[i].offsetType, strm.total_in, strm.total_out));
                    break;
                }
                case Z_BUF_ERROR:
                {
                    #ifdef debug
                    cout<<"decompression buffer was too short"<<endl;
                    #endif // debug
                    memScale++;//increase buffer size for the next iteration
                    i--;//make sure that we are retrying at this offset until the buffer is finally large enough
                    break;
                }
                default://shit hit the fan, should never happen normally
                {
                    cout<<"inflate() failed with exit code:"<<ret<<endl;
                    pause();
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors and deallocate the decompression buffer
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                pause();
                return ret;
            }
            delete [] decompBuffer;
        }
        #ifdef debug
        else
        {
            cout<<"skipping offset #"<<i<<" ("<<offsetList[i].offset<<") because it cannot be a header"<<endl;
        }
        #endif // debug
    }
    #ifdef debug
    cout<<endl;
    cout<<"Decompressed 32K streams: "<<numDecomp32k<<endl;
    cout<<"type1: "<<type1<<endl;
    cout<<"type2: "<<type2<<endl;
    cout<<"type3: "<<type3<<endl;
    cout<<"type4: "<<type4<<endl;
    cout<<"Decompressed 16K streams: "<<numDecomp16k<<endl;
    cout<<"Decompressed 8K streams: "<<numDecomp8k<<endl;
    cout<<"Decompressed 4K streams: "<<numDecomp4k<<endl;
    cout<<"Decompressed 2K streams: "<<numDecomp2k<<endl;
    cout<<"Decompressed 1K streams: "<<numDecomp1k<<endl;
    cout<<"data errors: "<<dataErrors<<endl;
    cout<<"Total decompressed streams: "<<(numDecomp32k+numDecomp16k+numDecomp8k+numDecomp4k+numDecomp2k+numDecomp1k)<<endl;
    #endif // debug
    cout<<"Good offsets: "<<streamOffsetList.size()<<endl;
    offsetList.clear();
    offsetList.shrink_to_fit();

    pause();
    //PHASE 3
    //start trying to find the parameters to use for recompression
    int_fast64_t numGoodOffsets;
    int_fast64_t identicalBytes;
    int_fast64_t numFullmatch=0;
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
    int_fast64_t concentrate=-1;//only try to recompress the stream# givel here, -1 disables this and runs on all streams

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
            pause();
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
                #ifdef debug
                cout<<endl;
                cout<<"stream #"<<j<<" ready for recompression trials"<<endl;
                #endif // debug
                if (slowmode){
                    #ifdef debug
                    cout<<"   entering slow mode"<<endl;
                    cout<<"   stream type: "<<streamOffsetList[j].offsetType<<endl;
                    #endif // debug
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
                                #ifdef debug
                                /*cout<<"-------------------------"<<endl;
                                cout<<"   memlevel:"<<memlevel<<endl;
                                cout<<"   clevel:"<<clevel<<endl;
                                cout<<"   window:"<<window<<endl;*/
                                #endif // debug
                                //use all default settings except clevel and memlevel
                                ret = deflateInit2(&strm1, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate stream init done"<<endl;
                                #endif // debug

                                //prepare for compressing in one pass
                                strm1.avail_in=streamOffsetList[j].inflatedLength;
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                strm1.next_out=recompBuffer;
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                                //check the return value to see if everything went well
                                if (ret != Z_STREAM_END){
                                    cout<<"recompression failed with exit code:"<<ret<<endl;
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate done"<<endl;
                                #endif // debug

                                //test if the recompressed stream matches the input data
                                if (strm1.total_out!=streamOffsetList[j].streamLength){
                                    identicalBytes=0;
                                    //cout<<"   size difference: "<<(strm1.total_out-streamOffsetList[j].streamLength)<<endl;
                                    if (abs((strm1.total_out-streamOffsetList[j].streamLength))>sizediffTresh){
                                        cout<<"   size difference is greater than "<<sizediffTresh<<" bytes, not comparing"<<endl;
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
                                        cout<<"   "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
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
                                                            cout<<"   first diff byte:"<<i<<endl;
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
                                                    streamOffsetList[j].diffByteOffsets.push_back(1);
                                                    streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+strm1.total_out+streamOffsetList[j].offset)]);
                                                    cout<<"   byte at the end added"<<endl;
                                                }
                                            } else {//the recompressed stream is longer than the original
                                                for (i=0; i<streamOffsetList[j].streamLength;i++){
                                                    if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                        if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                            streamOffsetList[j].firstDiffByte=(i);
                                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            cout<<"   first diff byte:"<<i<<endl;
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
                                    #ifdef debug
                                    cout<<"   stream sizes match, comparing"<<endl;
                                    #endif // debug
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamOffsetList[j].streamLength){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        #endif // debug
                                        fullmatch=true;
                                        numFullmatch++;
                                        streamOffsetList[j].identBytes=identicalBytes;
                                        streamOffsetList[j].clevel=clevel;
                                        streamOffsetList[j].memlvl=memlevel;
                                        streamOffsetList[j].window=window;
                                        streamOffsetList[j].firstDiffByte=-1;
                                        streamOffsetList[j].diffByteOffsets.clear();
                                        streamOffsetList[j].diffByteVal.clear();
                                    } else {
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                        if (((streamOffsetList[j].streamLength-identicalBytes)==2)&&((recompBuffer[0]-rBuffer[streamOffsetList[j].offset])!=0)&&((recompBuffer[1]-rBuffer[(1+streamOffsetList[j].offset)])!=0)){
                                            cout<<"   2 byte header mismatch, accepting"<<endl;
                                            fullmatch=true;
                                            numFullmatch++;
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
                                            cout<<"   1 byte header mismatch, accepting"<<endl;
                                            fullmatch=true;
                                            numFullmatch++;
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
                                                        cout<<"   first diff byte:"<<i<<endl;
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
                                        clevel--;
                                    }
                                }

                                //deallocate the Zlib stream and check if it went well
                                ret=deflateEnd(&strm1);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                delete [] recompBuffer;
                                #ifdef debug
                                cout<<"   deflate stream end done"<<endl;
                                #endif // debug
                            } while ((!fullmatch)&&(clevel>=1));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        window--;
                    } while ((!fullmatch)&&(window>=10));
                } else {
                #ifdef debug
                cout<<"   entering optimized mode"<<endl;
                #endif // debug
                /*switch (streamOffsetList[j].offsetType){
                    case 1:{
                        #ifdef debug
                        cout<<"   stream type: 1"<<endl;
                        #endif // debug
                        do {
                            //resetting the variables
                            strm1.zalloc = Z_NULL;
                            strm1.zfree = Z_NULL;
                            strm1.opaque = Z_NULL;
                            strm1.next_in=decompBuffer;
                            #ifdef debug
                            cout<<"   memlevel:"<<memlevel<<endl;
                            #endif // debug
                            //use all default settings except clevel and memlevel
                            ret = deflateInit2(&strm1, 1, Z_DEFLATED, 15, memlevel, Z_DEFAULT_STRATEGY); //only try clevel 1, 0 would be no compression, would be pointless
                            if (ret != Z_OK)
                            {
                                cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                pause();
                                abort();
                            }
                            #ifdef debug
                            cout<<"   deflate stream init done"<<endl;
                            #endif // debug

                            //prepare for compressing in one pass
                            strm1.avail_in=streamOffsetList[j].inflatedLength;
                            unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                            strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                            strm1.next_out=recompBuffer;
                            ret=deflate(&strm1, Z_FINISH);//do the actual compression
                            //check the return value to see if everything went well
                            if (ret != Z_STREAM_END){
                                cout<<"recompression failed with exit code:"<<ret<<endl;
                                pause();
                                abort();
                            }
                            #ifdef debug
                            //cout<<"   deflate done"<<endl;
                            #endif // debug

                            //test if the recompressed stream matches the input data
                            if (strm1.total_out!=streamOffsetList[j].streamLength){
                                cout<<"   recompression failed, size difference"<<endl;
                                memlevel--;
                            } else {
                                #ifdef debug
                                cout<<"   stream sizes match, comparing"<<endl;
                                #endif // debug
                                identicalBytes=0;
                                for (i=0; i<strm1.total_out;i++){
                                    if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j].offset)])==0){
                                        identicalBytes++;
                                    }
                                }
                                if (identicalBytes==streamOffsetList[j].streamLength){
                                    #ifdef debug
                                    cout<<"   recompression succesful, full match"<<endl;
                                    #endif // debug
                                    fullmatch=true;
                                    numFullmatch++;
                                } else {
                                    #ifdef debug
                                    cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                    pause();
                                    #endif // debug
                                    memlevel--;
                                }
                            }

                            //deallocate the Zlib stream and check if it went well
                            ret=deflateEnd(&strm1);
                            if (ret != Z_OK)
                            {
                                cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                pause();
                                abort();
                            }
                            delete [] recompBuffer;
                            #ifdef debug
                            cout<<"   deflate stream end done"<<endl;
                            #endif // debug
                        } while ((!fullmatch)&&(memlevel>=1));
                        break;
                    }
                    case 4:{
                        #ifdef debug
                        cout<<"   stream type: 4"<<endl;
                        #endif // debug
                        do {
                            clevel=9;
                            do {
                                //resetting the variables
                                strm1.zalloc = Z_NULL;
                                strm1.zfree = Z_NULL;
                                strm1.opaque = Z_NULL;
                                strm1.next_in=decompBuffer;
                                #ifdef debug
                                cout<<"   memlevel:"<<memlevel<<endl;
                                cout<<"   clevel:"<<clevel<<endl;
                                #endif // debug
                                //use all default settings except clevel and memlevel
                                ret = deflateInit2(&strm1, clevel, Z_DEFLATED, 15, memlevel, Z_DEFAULT_STRATEGY);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                cout<<"   deflate stream init done"<<endl;
                                #endif // debug

                                //prepare for compressing in one pass
                                strm1.avail_in=streamOffsetList[j].inflatedLength;
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                strm1.next_out=recompBuffer;
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                                //check the return value to see if everything went well
                                if (ret != Z_STREAM_END){
                                    cout<<"recompression failed with exit code:"<<ret<<endl;
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate done"<<endl;
                                #endif // debug

                                //test if the recompressed stream matches the input data
                                if (strm1.total_out!=streamOffsetList[j].streamLength){
                                    #ifdef debug
                                    cout<<"   recompression failed, size difference"<<endl;
                                    #endif // debug
                                    clevel--;
                                } else {
                                    #ifdef debug
                                    cout<<"   stream sizes match, comparing"<<endl;
                                    #endif // debug
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j].offset)])==0){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamOffsetList[j].streamLength){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        #endif // debug
                                        fullmatch=true;
                                        numFullmatch++;
                                    } else {
                                        #ifdef debug
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                        pause();
                                        #endif // debug
                                        clevel--;
                                    }
                                }

                                //deallocate the Zlib stream and check if it went well
                                ret=deflateEnd(&strm1);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                delete [] recompBuffer;
                                #ifdef debug
                                cout<<"   deflate stream end done"<<endl;
                                #endif // debug
                            } while ((!fullmatch)&&(clevel>=7));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        break;
                    }
                }*/
                }
                break;
            }
            case Z_DATA_ERROR: //the compressed data was invalid, this should never happen since the offsets have been checked
            {
                cout<<"inflate() failed with data error"<<endl;
                pause();
                abort();
            }
            case Z_BUF_ERROR: //this should not happen since the decompressed lengths are known
            {
                cout<<"inflate() failed with memory error"<<endl;
                pause();
                abort();
            }
            default: //shit hit the fan, should never happen normally
            {
                cout<<"inflate() failed with exit code:"<<ret<<endl;
                pause();
                abort();
            }
        }
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        ret=inflateEnd(&strm);
        if (ret!=Z_OK)
        {
            cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
            pause();
            abort();
        }
        delete [] decompBuffer;
    }
    if (concentrate>=0){
        numGoodOffsets=streamOffsetList.size();
    }
    cout<<endl;
    cout<<"recompressed streams:"<<numFullmatch<<" out of "<<numGoodOffsets<<endl;
    cout<<"streamOffsetList.size():"<<streamOffsetList.size()<<endl;
    cout<<endl;
    cout<<"Stream info"<<endl;
    for (j=0; j<streamOffsetList.size(); j++){
        cout<<"-------------------------"<<endl;
        cout<<"   stream #"<<j<<endl;
        cout<<"   offset:"<<streamOffsetList[j].offset<<endl;
        cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<endl;
        cout<<"   clevel:"<<+streamOffsetList[j].clevel<<endl;
        cout<<"   window:"<<+streamOffsetList[j].window<<endl;
        cout<<"   best match:"<<streamOffsetList[j].identBytes<<" out of "<<streamOffsetList[j].streamLength<<endl;
        cout<<"   diffBytes:"<<streamOffsetList[j].diffByteOffsets.size()<<endl;
        cout<<"   diffVals:"<<streamOffsetList[j].diffByteVal.size()<<endl;
        if (streamOffsetList[j].diffByteOffsets.size()<=recompTresh){
            recomp++;
            streamOffsetList[j].recomp=true;
        }
        cout<<"   mismatched bytes:";
        for (i=0; i<streamOffsetList[j].diffByteOffsets.size(); i++){
            cout<<streamOffsetList[j].diffByteOffsets[i]<<";";
        }
        cout<<endl;
    }
    cout<<"recompressed:"<<recomp<<"/"<<streamOffsetList.size()<<endl;
    pause();

    //PHASE 4
    //take the information created in phase 3 and use it to create an ATZ file(see ATZ file format spec.)
    std::ofstream outfile(filename_out, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outfile.is_open()) {
       cout << "error: open file for output failed!" << endl;
       pause();
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
            cout<<"recompressing stream #"<<j<<endl;
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
                pause();
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
                    pause();
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                pause();
                return ret;
            }
            outfile.write(reinterpret_cast<char*>(decompBuffer), streamOffsetList[j].inflatedLength);
            delete [] decompBuffer;
        }
    }

    uint64_t lastos=0;
    uint64_t lastlen=0;
    for(j=0;j<streamOffsetList.size();j++){//write the gaps before streams and non-recompressed streams to disk as the residue
        if ((lastos+lastlen)==streamOffsetList[j].offset){
            cout<<"no gap before stream #"<<j<<endl;
            if (streamOffsetList[j].recomp==false){
                cout<<"copying stream #"<<j<<endl;
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }else{
            cout<<"gap of "<<(streamOffsetList[j].offset-(lastos+lastlen))<<" bytes before stream #"<<j<<endl;
            outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (streamOffsetList[j].offset-(lastos+lastlen)));
            if (streamOffsetList[j].recomp==false){
                cout<<"copying stream #"<<j<<endl;
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }
        lastos=streamOffsetList[j].offset;
        lastlen=streamOffsetList[j].streamLength;
    }
    if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
        cout<<(infileSize-(lastos+lastlen))<<" bytes copied from the end of the file"<<endl;
        outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (infileSize-(lastos+lastlen)));
    }

    atzlen=outfile.tellp();
    cout<<"Total bytes written: "<<atzlen<<endl;
    outfile.seekp(4);
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    pause();
    streamOffsetList.clear();
    streamOffsetList.shrink_to_fit();
    outfile.close();

    //PHASE 5: verify that we can reconstruct the original file, using only data from the ATZ file
    infileSize=0;
    atzlen=0;
    lastos=28;
    uint64_t origlen=0;
    uint64_t nstrms=0;

    std::ifstream atzfile(filename_out, std::ios::in | std::ios::binary);
	if (!atzfile.is_open()) {
       cout << "error: open ATZ file for input failed!" << endl;
       pause();
 	   abort();
	}
    if (stat(filename_out, &statresults) == 0){
    	cout<<"File size:"<<statresults.st_size<<endl;
    }
    else{
    	cout<<"Error determining file size."<<endl;
    	pause();
    	abort();
    }
    infileSize=statresults.st_size;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* atzBuffer = new unsigned char[infileSize];
    atzfile.read(reinterpret_cast<char*>(atzBuffer), infileSize);
    atzfile.close();

    if ((atzBuffer[0]!=65)||(atzBuffer[1]!=84)||(atzBuffer[2]!=90)||(atzBuffer[3]!=1)){
        cout<<"ATZ1 header not found"<<endl;
        pause();
        abort();
    }
    atzlen=*reinterpret_cast<uint64_t*>(&atzBuffer[4]);
    if (atzlen!=infileSize){
        cout<<"atzlen mismatch"<<endl;
        pause();
        abort();
    }
    origlen=*reinterpret_cast<uint64_t*>(&atzBuffer[12]);
    nstrms=*reinterpret_cast<uint64_t*>(&atzBuffer[20]);
    cout<<"nstrms:"<<nstrms<<endl;

    if (nstrms>0){
        streamOffsetList.reserve(nstrms);
        //reead in all the info about the streams
        for (j=0;j<nstrms;j++){
            cout<<"stream #"<<j<<endl;
            streamOffsetList.push_back(streamOffset(*reinterpret_cast<uint64_t*>(&atzBuffer[lastos]), -1, *reinterpret_cast<uint64_t*>(&atzBuffer[8+lastos]), *reinterpret_cast<uint64_t*>(&atzBuffer[16+lastos])));
            streamOffsetList[j].clevel=atzBuffer[24+lastos];
            streamOffsetList[j].window=atzBuffer[25+lastos];
            streamOffsetList[j].memlvl=atzBuffer[26+lastos];
            cout<<"   offset:"<<streamOffsetList[j].offset<<endl;
            cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<endl;
            cout<<"   clevel:"<<+streamOffsetList[j].clevel<<endl;
            cout<<"   window:"<<+streamOffsetList[j].window<<endl;
            //pause();
            //partial match handling
            uint64_t diffbytes=*reinterpret_cast<uint64_t*>(&atzBuffer[27+lastos]);
            if (diffbytes>0){
                streamOffsetList[j].firstDiffByte=*reinterpret_cast<uint64_t*>(&atzBuffer[35+lastos]);
                streamOffsetList[j].diffByteOffsets.reserve(diffbytes);
                streamOffsetList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamOffsetList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&atzBuffer[43+8*i+lastos]));
                    streamOffsetList[j].diffByteVal.push_back(atzBuffer[43+diffbytes*8+i+lastos]);
                }
                streamOffsetList[j].atzInfos=&atzBuffer[43+diffbytes*9+lastos];
                lastos=lastos+43+diffbytes*9+streamOffsetList[j].inflatedLength;
            } else{
                streamOffsetList[j].firstDiffByte=-1;
                //cout<<"lastos:"<<lastos<<" atzBuffer:"<<reinterpret_cast<uint16_t*>(atzBuffer)<<" atzBuffer:"<<reinterpret_cast<uint16_t*>(&atzBuffer[0+lastos])<<endl;
                //pause();
                streamOffsetList[j].atzInfos=&atzBuffer[35+lastos];
                lastos=lastos+35+streamOffsetList[j].inflatedLength;
            }
        }
        //do the reconstructing
        lastos=0;
        lastlen=0;
        std::ofstream recfile(reconfile, std::ios::out | std::ios::binary | std::ios::trunc);
        for(j=0;j<streamOffsetList.size();j++){
            if ((lastos+lastlen)==streamOffsetList[j].offset){//no gap before the stream
                cout<<"no gap before stream #"<<j<<endl;
                cout<<"reconstructing stream #"<<j<<endl;
                {
                    //do compression
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //cout<<reinterpret_cast<uint16_t*>(atzBuffer)<<"; "<<streamOffsetList[j].atzInfos;
                    //pause();
                    //cout<<"; "<<reinterpret_cast<uint16_t*>(strm.next_in)<<endl;
                    //pause();
                    //initialize the stream for compression and check for error
                    ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
                        pause();
                        abort();
                    }
                    //a buffer needs to be created to hold the resulting compressed data
                    unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength];
                    strm.next_out=compBuffer;
                    strm.avail_out=streamOffsetList[j].streamLength;
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
                            pause();
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pause();
                        return ret;
                    }
                    recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                    delete [] compBuffer;
                }
            }else{/*
                cout<<"gap of "<<(streamOffsetList[j].offset-(lastos+lastlen))<<" bytes before stream #"<<j<<endl;
                outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (streamOffsetList[j].offset-(lastos+lastlen)));
                if (streamOffsetList[j].recomp==false){
                    cout<<"copying stream #"<<j<<endl;
                    outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
                }*/
            }
            lastos=streamOffsetList[j].offset;
            lastlen=streamOffsetList[j].streamLength;
        }
        recfile.close();
    }


    pause();
    delete [] rBuffer;
	return 0;
}
