#include <stdio.h>
#include <string.h>
int main() {
    char s[] = "LOGIN admin admin123";
    char *sp;
    char *cmd = strtok_r(s, " \n\r", &sp);
    char *args = strtok_r(NULL, "", &sp);
    printf("cmd: '%s', args: '%s'\n", cmd, args);
    return 0;
}
