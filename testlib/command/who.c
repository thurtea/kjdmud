int main(string arg) {
    object *list;
    int j;

    printf("%-25s idle\n", "name (*edit, +input)");
    printf("--------------------      ----\n");
    list = users();
    for (j = 0; j < sizeof(list); j++) {
        printf("%-25s %4d\n",
            (string)list[j]->query_name(),
            query_idle(list[j]) / 60);
    }
    return 1;
}
