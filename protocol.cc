#include <stdio.h>
#include <curl/curl.h>
 
int main(void)
{
  CURL *curl;
  CURLcode res;
 
  static const char *postthis="{ \"size\": \"4\", \"operators\": \"fold,tfold\" }";

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, "http://icfpc2013.cloudapp.net/train?auth=0451EUqILPkx1zWe7fD4BMiNzwHIjPGCkbKYFxI0vpsH1H");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postthis);
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
 
    /* always cleanup */ 
    curl_easy_cleanup(curl);
  }
  return 0;
}