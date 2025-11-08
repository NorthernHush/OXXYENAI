#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    const char *url;
    const char *title_xpath;
    const char *content_xpath;
    const char *category;
} datasetConfigs;

extern const datasetConfigs SITES[] = {
    // === C Programming (Theory & Tutorials) ===
    {"https://www.cprogramming.com/tutorial/c/lesson1.html", "//h1", "//div[@id='content']//p", "C"},
    {"https://www.cprogramming.com/tutorial/c/lesson2.html", "//h1", "//div[@id='content']//p", "C"},
    {"https://www.cs.cf.ac.uk/Dave/C/CE.html", "//h2[1]", "//blockquote | //p", "C"},
    {"https://www.gnu.org/software/gnu-c-manual/gnu-c-manual.html", "//h1", "//div[@class='chapter']//p", "C"},
    {"https://en.cppreference.com/w/c", "//h1", "//div[@id='toc']//following::p | //div[@class='t-navbar']//following::p", "C"},

    // === C Standard & References ===
    {"https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf", "", "", "C"}, // ISO C11 draft (PDF — требует отдельной обработки)
    {"https://port70.net/~nsz/c/c11/n1570.html", "//title", "//body//p | //pre", "C"}, // HTML-версия C11

    // === Practical C & System Programming ===
    {"https://github.com/brennanh/C-Programming-FAQ", "//h1", "//article//p | //li", "C"},
    {"https://github.com/angrave/SystemProgramming/wiki", "//h1", "//div[contains(@class,'wiki')]//p", "System"},
    {"https://piazza.com/class_profile/get_resource/je1u722l7d83jw/je1u7k238x67r8", "", "", "C"}, // (пример — может быть недоступен)

    // === Linux & System ===
    {"https://www.tldp.org/LDP/intro-linux/html/index.html", "//title", "//div[@class='toc']//following::p", "Linux"},
    {"https://www.tldp.org/LDP/Bash-Beginners-Guide/html/index.html", "//h1", "//div[@class='section']//p", "Linux"},
    {"https://www.tldp.org/LDP/abs/html/", "//h1", "//div[@class='section']//p", "Linux"},
    {"https://linux.die.net/", "//h1", "//p", "Linux"},
    {"https://www.linuxtopia.org/online_books/linux_administrator_guide/", "//h1", "//div[@class='section']//p", "Linux"},

    // === System Calls & Kernel ===
    {"https://man7.org/linux/man-pages/man2/intro.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man2/fork.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man2/execve.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man3/malloc.3.html", "//h1", "//div[@class='section']//p", "C"},
    {"https://www.win.tue.nl/~aeb/linux/lk/lk.html", "//h1", "//ul/following::p", "Kernel"},

    // === Networking & IPC ===
    {"https://beej.us/guide/bgnet/html/", "//h1", "//div[@class='refsect1']//p | //p", "Networking"},
    {"https://beej.us/guide/bgipc/html/", "//h1", "//div[@class='refsect1']//p | //p", "IPC"},

    // === POSIX ===
    {"https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html", "//h1", "//div[@class='section']//p", "POSIX"},
    {"https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap03.html", "//h1", "//div[@class='section']//p", "POSIX"},

    // === BSD ===
    {"https://docs.freebsd.org/en/books/handbook/introduction/", "//h1", "//div[@class='section']//p", "BSD"},
    {"https://docs.freebsd.org/en/books/handbook/basics/", "//h1", "//div[@class='section']//p", "BSD"},
    {"https://www.openbsd.org/faq/faq1.html", "//h1", "//blockquote | //p", "BSD"},
    {"https://www.openbsd.org/faq/faq2.html", "//h1", "//blockquote | //p", "BSD"},

    // === C Code Datasets (Public Repos) ===
    {"https://github.com/github/codeql-go/tree/main/ql/src/Security/CWE", "//h1", "//p | //li", "C"},
    {"https://github.com/EFForg/https-everywhere/tree/master/src/chrome/content/rules", "", "", "C"}, // (пример — не C, но показывает структуру)
    {"https://github.com/llvm/llvm-project/tree/main/clang-tools-extra/clang-tidy", "//h1", "//p", "C"},
    {"https://github.com/torvalds/linux/tree/master/samples", "//h1", "//p", "Kernel"},

    // === Local Dataset File (your own C code corpus) ===
    {"file://./dataset.c", "//comment | //function", "//function | //declaration", "C"}
};

// Общее количество сайтов + локальный файл
#define SITE_COUNT (sizeof(SITES) / sizeof(SITES[0]))

#endif // CONFIG_H