# mrcc - A Distributed C Compiler System on MapReduce #

*Notes: this tool was written in around year 2009 to 2010. It may need significant modifications to work with latest Hadoop and gcc compilers. /Eric*

## Hompage ##

Homepage: https://www.ericzma.com/projects/mrcc/ .

## Introduction ##

mrcc is an open source compilation system that uses MapReduce to distribute C code compilation across
the servers in the cloud computing platform. mrcc is originally built to use Hadoop, but it is easy to
transform it to other could computing platform by only changing the interface to the computing platform.

Introduction to the architecture, implementation and instalation instruction of mrcc can be found in
the [mrcc document](https://www.systutorials.com/699/mrcc-a-distributed-c-compiler-system-on-mapreduce/).

## License ##

GPL

## Authors ##

- [Eric Z. Ma](https://www.ericzma.com/)
- distcc authors (the code base includes distcc codes. Kudos to distcc developers!)
