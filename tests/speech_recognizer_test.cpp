#include "speech_recognizer.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    assert(trimTranscript("hello\n") == "hello");
    assert(trimTranscript("hello world\r\n") == "hello world");
    assert(trimTranscript("hello\r\n\r\n") == "hello");
    assert(trimTranscript("trailing tabs\t\t") == "trailing tabs");
    assert(trimTranscript("trailing spaces   ") == "trailing spaces");

    // Only trailing whitespace is trimmed; leading is preserved.
    assert(trimTranscript("  spaced  ") == "  spaced");

    // Edge cases.
    assert(trimTranscript("") == "");
    assert(trimTranscript("\n\r\t ") == "");
    assert(trimTranscript("no-trim") == "no-trim");

    std::cout << "speech_recognizer_test: OK" << std::endl;
    return 0;
}
