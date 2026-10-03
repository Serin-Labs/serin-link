/* Simulator callbacks: requests are observed; tests choose when replies arrive. */
static uint32_t s_test_wifi_session;
void sim_wifi_session_start(uint32_t session)
{
    s_test_wifi_session = session;
    sim_cmd_on_wifi_setup();
}
void sim_wifi_cancel(uint32_t session)
{
    printf("CMD: wifi cancel %u\n", (unsigned)session);
}
