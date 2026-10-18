#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#ifdef ESP_PLATFORM
#include "esp_netif.h"
#else
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#ifdef ESP_PLATFORM
char * getLocalIp() {
    static char ip_str[16];
    ip_str[0] = '\0';
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) {
        strcpy(ip_str, "0.0.0.0");
        return ip_str;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 (int)IP2STR(&ip.ip));
        return ip_str;
    }
    strcpy(ip_str, "0.0.0.0");
    return ip_str;
}
#else
char * getLocalIp() {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];
    char * ip4Address;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }


    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if((strcmp(ifa->ifa_name,"lo0")!=0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            printf("\tInterface : <%s>\n",ifa->ifa_name );
            printf("\t  Address : <%s>\n", host);
            asprintf(&ip4Address, "%s", host);
        }
    }

    freeifaddrs(ifaddr);
    return ip4Address;
}
#endif
