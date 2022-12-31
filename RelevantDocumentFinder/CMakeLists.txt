add_executable (RelevantDocumentFinder "Corpus.cpp" "Corpus.hpp" "catch.hpp" "CorpusTests.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET RelevantDocumentFinder PROPERTY CXX_STANDARD 20)
endif()