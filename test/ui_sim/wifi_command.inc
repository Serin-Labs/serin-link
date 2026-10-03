if (!strcmp(cmd, "wifi_ack")) {
    char *status = strtok_r(NULL, " \t\r\n", &save);
    char *session = strtok_r(NULL, " \t\r\n", &save);
    if (!status) return err("wifi_ack: <status> [session]");
    ui_wifi_cancel_result(session ? (uint32_t)strtoul(session, NULL, 0) : s_test_wifi_session,
                          (uint8_t)atoi(status));
    return 0;
}
