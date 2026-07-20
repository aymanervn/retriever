#include "core.h"

static int fails;

static void expect_match(const char *pat, const char *s, int want)
{
    int got = re_match(pat, s, strlen(s));
    if (got != want)
    {
        printf("FAIL match: pat=\"%s\" name=\"%s\" got=%d want=%d\n", pat, s, got, want);
        fails++;
    }
}

static void expect_valid(const char *pat, int want_ok)
{
    const char *err = re_valid(pat);
    if ((err == NULL) != (want_ok != 0))
    {
        printf("FAIL valid: pat=\"%s\" -> %s\n", pat, err ? err : "ok");
        fails++;
    }
}

int main(void)
{
    expect_match("report", "report", 1);
    expect_match("report", "Report", 1);
    expect_match("REPORT", "report", 1);
    expect_match("report", "reports", 0);
    expect_match("report", "repor", 0);
    expect_match("", "", 1);
    expect_match("", "x", 0);

    expect_match("img_\\d.jpg", "img_7.jpg", 1);
    expect_match("img_\\d.jpg", "IMG_20260720.JPG", 1);
    expect_match("img_\\d.jpg", "img_.jpg", 0);
    expect_match("img_\\d.jpg", "img_x.jpg", 0);
    expect_match("img_\\d.jpg", "ximg_7.jpg", 0);
    expect_match("img_\\d.jpg", "img_7.jpgx", 0);
    expect_match("\\d", "0", 1);
    expect_match("\\d", "123456789012345678901234567890", 1);
    expect_match("\\d", "", 0);
    expect_match("\\d", "12a", 0);
    expect_match("\\d\\d", "12", 1);
    expect_match("\\d\\d", "1", 0);

    expect_match("\\a", "hello", 1);
    expect_match("\\a", "HELLO", 1);
    expect_match("\\a", "he7lo", 0);
    expect_match("\\a", "", 0);
    expect_match("\\ab", "aab", 1);
    expect_match("\\ab", "ab", 1);
    expect_match("\\ab", "b", 0);
    expect_match("\\a\\d\\a", "ab12cd", 1);
    expect_match("\\a\\d\\a", "ab12", 0);

    expect_match("\\p", "-._ ", 1);
    expect_match("\\p", "a-b", 0);
    expect_match("\\p", "", 0);
    expect_match("\\a\\p\\a", "tar.gz", 1);
    expect_match("\\a\\p\\a", "tar-gz", 1);
    expect_match("\\a\\p\\a", "targz", 0);
    expect_match("\\d\\p\\d", "1.2", 1);

    expect_match("\\a.\\a", "tar.gz", 1);
    expect_match("\\a.\\a", "tarxgz", 0);
    expect_match("*", "*", 1);
    expect_match("*", "x", 0);
    expect_match("a+b", "a+b", 1);
    expect_match("a+b", "aab", 0);
    expect_match("a\\\\b", "a\\b", 1);
    expect_match("a\\\\b", "ab", 0);

    expect_valid("img_\\d.jpg", 1);
    expect_valid("\\a\\p\\d\\\\", 1);
    expect_valid("", 1);
    expect_valid("plain literal", 1);
    expect_valid("\\q", 0);
    expect_valid("trailing\\", 0);

    if (fails)
        return 1;
    puts("re_test: all tests passed");
    return 0;
}
