# Localization File Format.

And converts it to a custom file format that has an offset table and the strings.
We avoid hash collisions in the offset table by making the table 8x bigger than what is necessary.
This made the implementation simpler, but if it presents any problems, I am happy to change it to use closed / open addressing.
This library is made up of the generator and the runtime loader.
loc_file_gen.c is the generator and loc.h is the loader.
To compile the generator, just do:
`` cc loc_file_gen.c -o loc_gen ``
To use the loader, define LOC_IMPLEMENTATION in one file, and include in all the others.
```C
#define LOC_IMPLEMENTATION
#include "loc.h"
```
THIS FORMAT ONLY WORKS IF ALL OF YOUR STRINGS ARE UTF-8.

## Basic Usage for the Localization File Generator
This example generates three files: one for english, one for french, and one for spanish.
The format of the file looks like this:
hello | bonjour | buenas dias
thank you | merci | Gracias
```sh
loc_gen strings.txt en fr sp
```
## Basic Usage for the Localization File loader
```C
#include <stdio.h>

#define LOC_IMPLEMENTATION
#include "loc_loader.h"

int main(void) {
    /* loading french localization file */
    loc_file file = loc_load("strings.fr.loc");

    /* null-terminated string. You can just use printf on it */
    const char *loc_string = loc_get_string(&file, "hi there!");
    printf("%s\n", loc_string);

    loc_free(file);

    return 0;
}
```
