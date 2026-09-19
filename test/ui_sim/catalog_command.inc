/* Test-only simulator command: stable catalog IDs and empty catalogs. */
if (!strcmp(cmd, "roomsrc_ids")) {
    char *selected = strtok_r(NULL, " \t\r\n", &save);
    char *list = strtok_r(NULL, " \t\r\n", &save);
    if (!selected || !list) return err("roomsrc_ids: <selected_id> <id:name,...|->");
    struct sl2_room_source_entry entries[SL2_ROOM_CATALOG_MAX] = {0};
    int count = 0;
    char *item_save = NULL;
    if (strcmp(list, "-")) {
        for (char *item = strtok_r(list, ",", &item_save); item;
             item = strtok_r(NULL, ",", &item_save)) {
            char *colon = strchr(item, ':');
            if (!colon || count == SL2_ROOM_CATALOG_MAX) return err("roomsrc_ids: bad entry");
            *colon = '\0';
            entries[count].id = strtoull(item, NULL, 0);
            entries[count].flags = SL2_ROOM_SOURCE_F_SELECTABLE;
            snprintf(entries[count].name, sizeof entries[count].name, "%s", colon + 1);
            count++;
        }
    }
    ui_set_room_sources(entries, count, strtoull(selected, NULL, 0), SL2_ROOMST_OK);
    return 0;
}
