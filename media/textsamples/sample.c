/* sample.c — C syntax-highlighting sample for the UltraCanvas demo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 64

typedef struct {
    char  name[32];
    int   quantity;
    double price;
} Item;

static double total_value(const Item *items, size_t count) {
    double total = 0.0;
    for (size_t i = 0; i < count; ++i) {
        total += items[i].quantity * items[i].price;
    }
    return total;
}

int main(void) {
    Item cart[MAX_ITEMS];
    size_t count = 0;

    strcpy(cart[count].name, "widget");
    cart[count].quantity = 3;
    cart[count].price = 4.50;
    ++count;

    strcpy(cart[count].name, "gadget");
    cart[count].quantity = 1;
    cart[count].price = 19.99;
    ++count;

    printf("Cart holds %zu line(s), total = %.2f\n", count, total_value(cart, count));
    return EXIT_SUCCESS;
}
