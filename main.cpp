#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <zlib.h>

#define filename "test.bin"

void pause(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}


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
    int_fast64_t infileSize=statresults.st_size;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* rBuffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
    infile.close();

    //PHASE 1
	//search the file for zlib headers, count them and create an offset list
	/*
		objects created:
			offsetList: vector holding the offsets of potential zlib streams
			offsetType: vector holding information about the type of the potential offsets in offsetList. Index-coupled to offsetList.
		objects destroyed:
			none
		objects created, but not provided or destroyed:
			none
		variables declared:
			i: general purpose iterator, for for loops etc.
		debug variables declared:
			nMatch1..6: number of header matches found with 32K..1K window
		required:
			infileSize
			rBuffer
		provides:
			offsetList
			offsetType
			i
	*/
	#ifdef debug
	uint_fast64_t nMatch1=0;
	uint_fast64_t nMatch2=0;
	uint_fast64_t nMatch3=0;
	uint_fast64_t nMatch4=0;
	uint_fast64_t nMatch5=0;
	uint_fast64_t nMatch6=0;
	#endif
	//offsetList stores memory offsets where potential headers can be found
	//offsetType stores the type of the header
	vector<int_fast64_t> offsetList;
	vector<int_fast32_t> offsetType;
	int_fast64_t i;
	//try to guess the number of potential zlib headers in the file from the file size
	//this value is purely empirical, may need tweaking
	offsetList.reserve(static_cast<int_fast64_t>(infileSize/1912));
	offsetType.reserve(static_cast<int_fast64_t>(infileSize/1912));
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
                        offsetList.push_back(i);
                        offsetType.push_back(1);
                        break;
                    }
                    case 94:{//hex 78 5E
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 5E) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(2);
                        break;
                    }
                    case 156:{//hex 78 9C
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 9C) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(3);
                        break;
                    }
                    case 218:{//hex 78 DA
                        #ifdef debug
                        nMatch1++;
                        cout<<"Found zlib header(78 DA) with 32K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(4);
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
                        offsetList.push_back(i);
                        offsetType.push_back(5);
                        break;
                    }
                    case 129:{//hex 68 81
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 81) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(6);
                        break;
                    }
                    case 67:{//hex 68 43
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 43) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(7);
                        break;
                    }
                    case 5:{//hex 68 05
                        #ifdef debug
                        nMatch2++;
                        cout<<"Found zlib header(68 05) with 16K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(8);
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
                        offsetList.push_back(i);
                        offsetType.push_back(9);
                        break;
                    }
                    case 133:{//hex 58 85
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 85) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(10);
                        break;
                    }
                    case 71:{//hex 58 47
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 47) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(11);
                        break;
                    }
                    case 9:{//hex 58 09
                        #ifdef debug
                        nMatch3++;
                        cout<<"Found zlib header(58 09) with 8K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(12);
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
                        offsetList.push_back(i);
                        offsetType.push_back(13);
                        break;
                    }
                    case 137:{//hex 48 89
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 89) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(14);
                        break;
                    }
                    case 75:{//hex 48 4B
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 4B) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(15);
                        break;
                    }
                    case 13:{//hex 48 0D
                        #ifdef debug
                        nMatch4++;
                        cout<<"Found zlib header(48 0D) with 4K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(16);
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
                        offsetList.push_back(i);
                        offsetType.push_back(17);
                        break;
                    }
                    case 141:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 8D) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(18);
                        break;
                    }
                    case 79:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 4F) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(19);
                        break;
                    }
                    case 17:{
                        #ifdef debug
                        nMatch5++;
                        cout<<"Found zlib header(38 11) with 2K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(20);
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
                        offsetList.push_back(i);
                        offsetType.push_back(21);
                        break;
                    }
                    case 145:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 91) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(22);
                        break;
                    }
                    case 83:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 53) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(23);
                        break;
                    }
                    case 21:{
                        #ifdef debug
                        nMatch6++;
                        cout<<"Found zlib header(28 15) with 1K window at offset: "<<i<<endl;
                        #endif // debug
                        offsetList.push_back(i);
                        offsetType.push_back(24);
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
    int_fast64_t j;
	int_fast64_t numOffsets;
	int_fast32_t ret;
    vector<int_fast64_t> streamOffsetList;
	vector<int_fast32_t> streamType;
	vector<int_fast64_t> streamLength;
	vector<int_fast64_t> inflatedLength;
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
    for (j=0; j<numOffsets; j++)
    {
        if ((lastGoodOffset+lastStreamLength)<=offsetList[j])
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
            strm.avail_in= infileSize-offsetList[j];
            strm.next_in=rBuffer+offsetList[j];//this is effectively adding an integer to a pointer, resulting in a pointer
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
            unsigned char* decompBuffer= new unsigned char[(memScale*5*(infileSize-offsetList[j]))]; //just a wild guess, corresponds to a compression ratio of 20%
            strm.next_out=decompBuffer;
            strm.avail_out=memScale*5*(infileSize-offsetList[j]);
            ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
            //check the return value
            switch (ret)
            {
                case Z_STREAM_END://decompression was succesful
                {
                    #ifdef debug
                    switch(offsetType[j]){
                        //32K window streams
                        case 1:{//78 01
                            cout<<"Stream #"<<j<<"(78 01) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type1++;
                            break;
                        }
                        case 2:{//78 5E
                            cout<<"Stream #"<<j<<"(78 5E) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type2++;
                            break;
                        }
                        case 3:{//78 9C
                            cout<<"Stream #"<<j<<"(78 9C) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type3++;
                            break;
                        }
                        case 4:{//78 DA
                            cout<<"Stream #"<<j<<"(78 DA) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp32k++;
                            type4++;
                            break;
                        }
                        //16K window streams
                        case 5:{//68 DE
                            cout<<"Stream #"<<j<<"(68 DE) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 6:{//68 81
                            cout<<"Stream #"<<j<<"(68 81) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 7:{//68 43
                            cout<<"Stream #"<<j<<"(68 43) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        case 8:{//68 05
                            cout<<"Stream #"<<j<<"(68 05) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp16k++;
                            break;
                        }
                        //8K window streams
                        case 9:{//58 C3
                            cout<<"Stream #"<<j<<"(58 C3) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 10:{//58 85
                            cout<<"Stream #"<<j<<"(58 85) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 11:{//58 47
                            cout<<"Stream #"<<j<<"(58 47) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        case 12:{//58 09
                            cout<<"Stream #"<<j<<"(58 09) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp8k++;
                            break;
                        }
                        //4K window streams
                        case 13:{
                            cout<<"Stream #"<<j<<"(48 C7) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 14:{
                            cout<<"Stream #"<<j<<"(48 89) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 15:{
                            cout<<"Stream #"<<j<<"(48 4B) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        case 16:{
                            cout<<"Stream #"<<j<<"(48 0D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp4k++;
                            break;
                        }
                        //2K window streams
                        case 17:{
                            cout<<"Stream #"<<j<<"(38 CB) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 18:{
                            cout<<"Stream #"<<j<<"(38 8D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 19:{
                            cout<<"Stream #"<<j<<"(38 4F) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        case 20:{
                            cout<<"Stream #"<<j<<"(38 11) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp2k++;
                            break;
                        }
                        //1K window streams
                        case 21:{
                            cout<<"Stream #"<<j<<"(28 CF) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 22:{
                            cout<<"Stream #"<<j<<"(28 91) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 23:{
                            cout<<"Stream #"<<j<<"(28 53) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                        case 24:{
                            cout<<"Stream #"<<j<<"(28 15) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                            numDecomp1k++;
                            break;
                        }
                    }
                    #endif // debug
                    lastGoodOffset=offsetList[j];
                    lastStreamLength=strm.total_in;
                    streamOffsetList.push_back(offsetList[j]);
                    streamType.push_back(offsetType[j]);
                    streamLength.push_back(strm.total_in);
                    inflatedLength.push_back(strm.total_out);
                    break;
                }
                case Z_DATA_ERROR://the compressed data was invalid, most likely it was not a good offset
                {
                    #ifdef debug
                    dataErrors++;
                    #endif // debug
                    break;
                }
                case Z_BUF_ERROR:
                {
                    cout<<"decompression buffer was too short"<<endl;
                    memScale++;//increase buffer size for the next iteration
                    j--;//make sure that we are retrying at this offset until the buffer is finally large enough
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
            cout<<"skipping offset #"<<j<<" ("<<offsetList[j]<<") because it cannot be a header"<<endl;
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

    pause();
    //PHASE 3
    //start trying to find the parameters to use for recompression
    int_fast64_t numGoodOffsets;
    int_fast64_t identicalBytes;
    int_fast64_t numFullmatch=0;
    vector<int_fast64_t> recompOffsetList;
    vector<int_fast64_t> streamOffsetClevel;
    vector<int_fast64_t> streamOffsetMemlevel;
    vector<int_fast64_t> streamOffsetWindow;
    vector<int_fast64_t> streamOffsetHdrMismatch;
    vector<int_fast64_t> streamOffsetIdenticalBytes;
    int memlevel=9;
    int clevel=9;
    int window=15;
    int sizediffTresh=64;
    bool fullmatch=false;
    bool found=false;
    z_stream strm1;

    bool slowmode=true;
    #ifdef debug
    int_fast64_t concentrate=-1;//only try to recompress the stream# givel here, -1 for normal mode
    #endif // debug

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
        strm.avail_in = streamLength[j];
        strm.next_in=rBuffer+streamOffsetList[j];//this is effectively adding an integer to a pointer, resulting in a pointer
        //initialize the stream for decompression and check for error
        ret=inflateInit(&strm);
        if (ret != Z_OK)
        {
            cout<<"inflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
            pause();
            abort();
        }
        //a buffer needs to be created to hold the resulting decompressed data
        //since we have already deompressed the data before, we know how large of a buffer we need to allocate
        unsigned char* decompBuffer= new unsigned char[inflatedLength[j]];
        strm.next_out=decompBuffer;
        strm.avail_out=inflatedLength[j];
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
                    cout<<"   stream type: "<<streamType[j]<<endl;
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
                                cout<<"-------------------------"<<endl;
                                cout<<"   memlevel:"<<memlevel<<endl;
                                cout<<"   clevel:"<<clevel<<endl;
                                cout<<"   window:"<<window<<endl;
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
                                cout<<"   deflate stream init done"<<endl;
                                #endif // debug

                                //prepare for compressing in one pass
                                strm1.avail_in=inflatedLength[j];
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, inflatedLength[j])]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, inflatedLength[j]);
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
                                if (strm1.total_out!=streamLength[j]){
                                    identicalBytes=0;
                                    #ifdef debug
                                    cout<<"   size difference: "<<(strm1.total_out-streamLength[j])<<endl;
                                    if (abs((strm1.total_out-streamLength[j]))>sizediffTresh){
                                        cout<<"   size difference is greater than "<<sizediffTresh<<" bytes, not comparing"<<endl;
                                    } else {
                                        if (strm1.total_out<streamLength[j]){
                                            for (i=0; i<strm1.total_out;i++){
                                                if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                                                    identicalBytes++;
                                                }
                                            }
                                        } else {
                                            for (i=0; i<streamLength[j];i++){
                                                if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                                                    identicalBytes++;
                                                }
                                            }
                                        }
                                        cout<<"   "<<identicalBytes<<" bytes out of "<<streamLength[j]<<" identical"<<endl;
                                    }
                                    #endif // debug
                                    clevel--;
                                } else {
                                    #ifdef debug
                                    cout<<"   stream sizes match, comparing"<<endl;
                                    #endif // debug
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamLength[j]){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        #endif // debug
                                        fullmatch=true;
                                        numFullmatch++;
                                        recompOffsetList.push_back(streamOffsetList[j]);
                                        streamOffsetClevel.push_back(clevel);
                                        streamOffsetMemlevel.push_back(memlevel);
                                        streamOffsetWindow.push_back(window);
                                        streamOffsetHdrMismatch.push_back(0);
                                    } else {
                                        #ifdef debug
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamLength[j]<<" identical"<<endl;
                                        if (((streamLength[j]-identicalBytes)==2)&&((recompBuffer[0]-rBuffer[streamOffsetList[j]])!=0)&&((recompBuffer[1]-rBuffer[(1+streamOffsetList[j])])!=0)){
                                            cout<<"   2 byte header mismatch, accepting"<<endl;
                                            fullmatch=true;
                                            numFullmatch++;
                                            recompOffsetList.push_back(streamOffsetList[j]);
                                            streamOffsetClevel.push_back(clevel);
                                            streamOffsetMemlevel.push_back(memlevel);
                                            streamOffsetWindow.push_back(window);
                                            streamOffsetHdrMismatch.push_back(1);
                                        }
                                        if (((streamLength[j]-identicalBytes)==1)&&(((recompBuffer[0]-rBuffer[streamOffsetList[j]])!=0)||((recompBuffer[1]-rBuffer[(1+streamOffsetList[j])])!=0))){
                                            cout<<"   1 byte header mismatch, accepting"<<endl;
                                            fullmatch=true;
                                            numFullmatch++;
                                            recompOffsetList.push_back(streamOffsetList[j]);
                                            streamOffsetClevel.push_back(clevel);
                                            streamOffsetMemlevel.push_back(memlevel);
                                            streamOffsetWindow.push_back(window);
                                            streamOffsetHdrMismatch.push_back(1);
                                        }
                                        //pause();
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
                            } while ((!fullmatch)&&(clevel>=1));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        window--;
                    } while ((!fullmatch)&&(window>=10));
                } else {
                #ifdef debug
                cout<<"   entering optimized mode"<<endl;
                #endif // debug
                switch (streamType[j]){
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
                            strm1.avail_in=inflatedLength[j];
                            unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, inflatedLength[j])]; //allocate output for worst case
                            strm1.avail_out=deflateBound(&strm1, inflatedLength[j]);
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
                            if (strm1.total_out!=streamLength[j]){
                                cout<<"   recompression failed, size difference"<<endl;
                                memlevel--;
                            } else {
                                #ifdef debug
                                cout<<"   stream sizes match, comparing"<<endl;
                                #endif // debug
                                identicalBytes=0;
                                for (i=0; i<strm1.total_out;i++){
                                    if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                                        identicalBytes++;
                                    }
                                }
                                if (identicalBytes==streamLength[j]){
                                    #ifdef debug
                                    cout<<"   recompression succesful, full match"<<endl;
                                    #endif // debug
                                    fullmatch=true;
                                    numFullmatch++;
                                } else {
                                    #ifdef debug
                                    cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamLength[j]<<" identical"<<endl;
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
                                strm1.avail_in=inflatedLength[j];
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, inflatedLength[j])]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, inflatedLength[j]);
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
                                if (strm1.total_out!=streamLength[j]){
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
                                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamLength[j]){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        #endif // debug
                                        fullmatch=true;
                                        numFullmatch++;
                                    } else {
                                        #ifdef debug
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamLength[j]<<" identical"<<endl;
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
                }
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
    cout<<"recompOffsetList.szize():"<<recompOffsetList.size()<<endl;
    cout<<endl;
    cout<<"Recompressed stream info"<<endl;
    for (j=0; j<recompOffsetList.size(); j++){
        cout<<"-------------------------"<<endl;
        cout<<"   recompressed stream #"<<j<<endl;
        cout<<"   offset:"<<recompOffsetList[j]<<endl;
        cout<<"   memlevel:"<<streamOffsetMemlevel[j]<<endl;
        cout<<"   clevel:"<<streamOffsetClevel[j]<<endl;
        cout<<"   window:"<<streamOffsetWindow[j]<<endl;
        cout<<"   mismatched header bytes:"<<streamOffsetHdrMismatch[j]<<endl;
    }
    cout<<endl;
    cout<<"Streams not recompressed:"<<endl;
    for (j=0; j<numGoodOffsets; j++){
        found=false;
        for (i=0; i<recompOffsetList.size(); i++){
            if (recompOffsetList[i]==streamOffsetList[j]){
                found=true;
            }
            if (found) break;
        }
        if (!found) {
            cout<<"-------------------------"<<endl;
            cout<<"   stream #"<<j<<endl;
            cout<<"   offset:"<<streamOffsetList[j]<<endl;
            cout<<"   type:"<<streamType[j]<<endl;
        }
    }


    /*int_fast64_t numGoodOffsets=streamOffsetList.size();
    int_fast64_t identical=0;
    for (j=0; j<numGoodOffsets; j++)
    {
        //create a new Zlib stream to do decompression
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        //the lengths of the zlib streams have been saved by the previous phase
        strm.avail_in= streamLength[j];
        strm.next_in=rBuffer+streamOffsetList[j];//this is effectively adding an integer to a pointer, resulting in a pointer
        //initialize the stream for decompression and check for error
        int ret=inflateInit(&strm);
        if (ret != Z_OK)
        {
            cout<<"inflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
            pause();
            return ret;
        }
        //a buffer needs to be created to hold the resulting decompressed data
        //since we have already deompressed the data before, we know how large of a buffer we need to allocate
        unsigned char* decompBuffer= new unsigned char[inflatedLength[j]];
        strm.next_out=decompBuffer;
        strm.avail_out=inflatedLength[j];
        ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        //check the return value
        switch (ret){
        case Z_STREAM_END://decompression was succesful
        {
            cout<<"decompressed good offset #"<<j<<endl;
            z_stream recompstrm;
            recompstrm.zalloc = Z_NULL;
            recompstrm.zfree = Z_NULL;
            recompstrm.opaque = Z_NULL;
            recompstrm.next_in=decompBuffer;
            switch(offsetType[j]){
                //32K window streams
                case 1:{//78 01
                    cout<<"Stream #"<<j<<"(78 01) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    ret = deflateInit2(&recompstrm, 1, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);//1 is the compression level,15 is the 32K window size, 8 is the default memory level
                    if (ret != Z_OK){
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
                        pause();
                        return ret;
                    }
                    //prepare for compressing in one pass
                    recompstrm.avail_in=inflatedLength[j];
                    unsigned char* recompBuffer=new unsigned char[deflateBound(&recompstrm, inflatedLength[j])]; //allocate output for worst case
                    recompstrm.avail_out=deflateBound(&recompstrm, inflatedLength[j]);
                    recompstrm.next_out=recompBuffer;
                    ret=deflate(&recompstrm, Z_FINISH);
                    if (ret != Z_STREAM_END){
                        cout<<"recompression failed with exit code:"<<ret<<endl;
                        pause();
                        return ret;
                    }

                    if (recompstrm.total_out!=streamLength[j]){
                        cout<<"recompression failed, size difference"<<endl;
                        break;
                    }
                    for(i=0;i<streamLength[j];i++){
                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                            identical++;
                        }
                    }
                    cout<<"identical bytes: "<<identical<<" out of "<<streamLength[j]<<endl;
                    //deallocate the zlib stream, check for errors and deallocate the recompression buffer
                    ret=inflateEnd(&recompstrm);
                    if (ret!=Z_OK)
                    {
                        cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pause();
                        return ret;
                    }
                    delete [] recompBuffer;
                    break;
                }
                case 2:{//78 5E
                    cout<<"Stream #"<<j<<"(78 5E) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp32k++;
                    break;
                }
                case 3:{//78 9C
                    cout<<"Stream #"<<j<<"(78 9C) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp32k++;
                    break;
                }
                case 4:{//78 DA
                    cout<<"Stream #"<<j<<"(78 DA) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    ret = deflateInit2(&recompstrm, 9, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);//1 is the compression level,15 is the 32K window size, 8 is the default memory level
                    if (ret != Z_OK){
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
                        pause();
                        return ret;
                    }
                    //prepare for compressing in one pass
                    recompstrm.avail_in=inflatedLength[j];
                    unsigned char* recompBuffer=new unsigned char[deflateBound(&recompstrm, inflatedLength[j])]; //allocate output for worst case
                    recompstrm.avail_out=deflateBound(&recompstrm, inflatedLength[j]);
                    recompstrm.next_out=recompBuffer;
                    ret=deflate(&recompstrm, Z_FINISH);
                    if (ret != Z_STREAM_END){
                        cout<<"recompression failed with exit code:"<<ret<<endl;
                        pause();
                        return ret;
                    }

                    if (recompstrm.total_out!=streamLength[j]){
                        //cout<<"recompression failed, size difference"<<endl;
                        break;
                    }
                    for(i=0;i<streamLength[j];i++){
                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j])])==0){
                            identical++;
                        }
                    }
                    //cout<<"identical bytes: "<<identical<<" out of "<<streamLength[j]<<endl;
                    //deallocate the zlib stream, check for errors and deallocate the recompression buffer
                    ret=inflateEnd(&recompstrm);
                    if (ret!=Z_OK)
                    {
                        cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pause();
                        return ret;
                    }
                    delete [] recompBuffer;
                    break;
                }
                        //16K window streams
                case 5:{//68 DE
                    cout<<"Stream #"<<j<<"(68 DE) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp16k++;
                    break;
                }
                case 6:{//68 81
                    cout<<"Stream #"<<j<<"(68 81) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp16k++;
                    break;
                }
                case 7:{//68 43
                    cout<<"Stream #"<<j<<"(68 43) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp16k++;
                    break;
                }
                case 8:{//68 05
                    cout<<"Stream #"<<j<<"(68 05) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp16k++;
                    break;
                }
                //8K window streams
                case 9:{//58 C3
                    cout<<"Stream #"<<j<<"(58 C3) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp8k++;
                    break;
                }
                case 10:{//58 85
                    cout<<"Stream #"<<j<<"(58 85) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp8k++;
                    break;
                }
                case 11:{//58 47
                    cout<<"Stream #"<<j<<"(58 47) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp8k++;
                    break;
                }
                case 12:{//58 09
                    cout<<"Stream #"<<j<<"(58 09) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp8k++;
                    break;
                }
                //4K window streams
                case 13:{
                    cout<<"Stream #"<<j<<"(48 C7) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp4k++;
                    break;
                }
                case 14:{
                    cout<<"Stream #"<<j<<"(48 89) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp4k++;
                    break;
                }
                case 15:{
                    cout<<"Stream #"<<j<<"(48 4B) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp4k++;
                    break;
                }
                case 16:{
                    cout<<"Stream #"<<j<<"(48 0D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp4k++;
                    break;
                }
                //2K window streams
                case 17:{
                    cout<<"Stream #"<<j<<"(38 CB) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp2k++;
                    break;
                }
                case 18:{
                    cout<<"Stream #"<<j<<"(38 8D) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp2k++;
                    break;
                }
                case 19:{
                    cout<<"Stream #"<<j<<"(38 4F) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp2k++;
                    break;
                }
                case 20:{
                    cout<<"Stream #"<<j<<"(38 11) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp2k++;
                    break;
                }
                //1K window streams
                case 21:{
                    cout<<"Stream #"<<j<<"(28 CF) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp1k++;
                    break;
                }
                case 22:{
                    cout<<"Stream #"<<j<<"(28 91) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp1k++;
                    break;
                }
                case 23:{
                    cout<<"Stream #"<<j<<"(28 53) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp1k++;
                    break;
                }
                case 24:{
                    cout<<"Stream #"<<j<<"(28 15) decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<endl;
                    numDecomp1k++;
                    break;
                }
            }
            break;
        }
        case Z_DATA_ERROR://the compressed data was invalid, most likely it was not a good offset
        {
            cout<<"inflate() failed with data error"<<endl;
            pause();
            return ret;
        }
        case Z_BUF_ERROR:
        {
            cout<<"inflate() failed with memory error"<<endl;
            pause();
            return ret;
        }
        default://shit hit the fan, should never happen normally
        {
            cout<<"inflate() failed with exit code:"<<ret<<endl;
            pause();
            return ret;
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
    }*/
    pause();
    delete [] rBuffer;
	return 0;
}
