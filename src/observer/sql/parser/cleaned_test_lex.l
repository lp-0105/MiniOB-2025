%{
#include <stdio.h>
%}
%%
"[^"]*"    { printf("Matched double quotes: %s\n", yytext); }
%%
int main() {
    yylex();
    return 0;
}