import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WindowsQuoteTests(unittest.TestCase):
    def test_arguments_with_spaces_quotes_and_trailing_slashes(self):
        # Literal Windows CRT command lines, independent of the quoting implementation.
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'quote.c'
            source.write_text(r'''
#include <assert.h>
#include <wchar.h>
#include "windows/quote.h"
int main(void) {
    wchar_t output[256];
    wchar_t *end = output;
    end = quote_argument(end, L"");
    assert(wcscmp(output, L"\"\"") == 0);
    end = quote_argument(output, L"spotify:track:a b");
    assert(wcscmp(output, L"\"spotify:track:a b\"") == 0);
    end = quote_argument(output, L"C:\\folder name\\");
    assert(wcscmp(output, L"\"C:\\folder name\\\\\"") == 0);
    end = quote_argument(output, L"a\\\"b");
    assert(wcscmp(output, L"\"a\\\\\\\"b\"") == 0);
    assert(*end == 0);
}
''')
            binary = Path(tmp) / 'quote-test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
