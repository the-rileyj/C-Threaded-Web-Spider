# Must be ran as root

# http://www.pcre.org/
sudo apt-get -y install libtool m4 automake

wget https://ftp.pcre.org/pub/pcre/pcre2-10.32.tar.gz

tar -xzf pcre2-10.32.tar.gz

cd ./pcre2-10.32

./configure

make

sudo make install

cd ..

git clone https://github.com/google/gumbo-parser

cd ./gumbo-parser

sudo sh ./autogen.sh

./configure

make

sudo make install

sudo ldconfig

### COMPILE WITH: ###
# gcc webScraper.c -lpcre2-8 -lgumbo