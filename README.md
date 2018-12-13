# Multithreaded C Web Spider

## Advisory

This scraper has bugs, it needs love even though it is functional; this project is not a priority, however there will be updates to this soon.

## Dependencies

Extra headers from the following projects (more information in install.sh):

* PCRE2
* Gumbo

To install the PCRE2 and Gumbo projects which are used in this, you will need:

* libtool
* m4
* automake

## Usage

```bash

# This won't actually work because it can currently only do HTTP
./webScraper 4 https://therileyjohnson.com

```

First arg is the number of threads you want to use, the second is the URL to spider.