# AntiZ

AntiZ is a project to create an open source precompressor for lightly compressed data.(currently Zlib)
Zlib is very common, for example it is used in PDF, JAR, ZIP, PNG etc.
It is fast but it has a poor compression ratio, and it is usually not effective to use a stronger compression(eg. LZMA) on data that has been compressed with Zlib. It would be much better if the data was not compressed at all before the sron compression. For example:
PDF file: 172KB---->compresses to 124KB with 7ZIP 9.38 beta (ultra preset)
PDF file: 172KB---->expands to 745KB with AntiZ----->compresses to 104KB with 7ZIP

Of course this process is not trivial if you want to get back the original file, byte identical.
This project is inspiried by and aims to be a replacement for the long abandoned precomp project (non open source).
http://schnaader.info/precomp.php
