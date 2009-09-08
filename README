Bag Of Words Library README
***************************

`libbow', version 1.0.

   Documentation and updates for `libbow' are available at
http://www.cs.cmu.edu/~mccallum/bow

   Rainbow is a C program that performs document classification using
one of several different methods, including naive Bayes, TFIDF/Rocchio,
K-nearest neighbor, Maximum Entropy, Support Vector Machines, Fuhr's
Probabilitistic Indexing, and a simple-minded form a shrinkage with
naive Bayes.

   Rainbow's accompanying library, `libbow', is a library of C code
intended for support of statistical text-processing programs.  The
current source distribution includes the library, a text classification
front-end (rainbow), a simple TFIDF-based document retrieval front-end
(arrow), an AltaVista-style document retrieval front-end (archer), and a
unsupported document clustering front-end with hierarchical clustering
and deterministic annealing (crossbow).

The library provides facilities for:
 *  Recursively descending directories, finding text files.
 *  Finding `document' boundaries when there are multiple docs per file.
 *  Tokenizing a text file, according to several different methods.
 *  Including N-grams among the tokens.
 *  Mapping strings to integers and back again, very efficiently.
 *  Building a sparse matrix of document/token counts.
 *  Pruning vocabulary by occurrence counts or by information gain.
 *  Building and manipulating word vectors.
 *  Setting word vector weights according to NaiveBayes, TFIDF, and a
     simple form of Probabilistic Indexing.
 *  Scoring queries for retrieval or classification.
 *  Writing all data structures to disk in a compact format.
 *  Reading the document/token matrix from disk in an efficient,
     sparse fashion.
 *  Performing test/train splits, and automatic classification tests.
 *  Operating in server mode, receiving and answering queries over a
     socket.

   It is known to compile on most UNIX systems, including Linux,
Solaris, SUNOS, Irix and HPUX.  Six months ago, it compiled on
WindowsNT (with a GNU build environment); it would probably work again
with little effort.  Patches to the code are most welcome.

   It is relatively efficient.  Reading, tokenizing and indexing the raw
text of 20,000 UseNet articles takes about 3 minutes.  Building a naive
Bayes classifier from 10,000 articles, and classifying the other 10,000
takes about 1 minute.

   The code conforms to the GNU coding standards.  It is released under
the Library GNU Public License (LGPL).

The library does not:
        Have parsing facilities.
        Do smoothing across N-gram models.
        Claim to be finished.
        Have good documentation.
        Claim to be bug-free.
        ...many other things.

Rainbow
=======

   `Rainbow' is a standalone program that does document classification.
Here are some examples:

   *      rainbow -i ./training/positive ./training/negative

     Using the text files found under the directories `./positive' and
     `./negative', tokenize, build word vectors, and write the
     resulting data structures to disk.

   *      rainbow --query=./testing/254

     Tokenize the text document `./testing/254', and classify it,
     producing output like:

          /home/mccallum/training/positive 0.72
          /home/mccallum/training/negative 0.28

   *      rainbow --test-set=0.5 -t 5

     Perform 5 trials, each consisting of a new random test/train split
     and outputs of the classification of the test documents.


   Typing `rainbow --help' will give list of all rainbow options.

   After you have compiled `libbow' and `rainbow', you can run the
shell script `./demo/script' to see an annotated demonstration of the
classifier in action.

   More information and documentation is available at
http://www.cs.cmu.edu/~mccallum/bow

Rainbow improvements coming eventually:
   Better documentation.
   Incremental model training.

Arrow
=====

   `Arrow' is a standalone program that does document retrieval by
TFIDF.

   Index all the documents in directory `foo' by typing

     arrow --index foo

   Make a single query by typing

     arrow --query

   then typing your query, and pressing Control-D.

   If you want to make many queries, it will be more efficient to run
arrow as a server, and query it multiple times without restarts by
communicating through a socket.  Type, for example,

     arrow --query-server=9876

   And access it through port number 9876.  For example:

     telnet localhost 9876

   In this mode there is no need to press Control-D to end a query.
Simply type your query on one line, and press return.

Crossbow
========

   `Crossbow' is a standalone program that does document clustering.
Sorry, there is no documentation yet.

Archer
======

   `Archer' is a standalone program that does document retrieval with
AltaVista-type queries, using +, -, "", etc.  The commands in the
"arrow" examples above also work for archer.  See "archer -help" for
more information.

