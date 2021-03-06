// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pagespeed/rules/avoid_landing_page_redirects.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/logging.h"
#include "googleurl/src/gurl.h"
#include "pagespeed/core/formatter.h"
#include "pagespeed/core/pagespeed_input.h"
#include "pagespeed/core/resource.h"
#include "pagespeed/core/resource_cache_computer.h"
#include "pagespeed/core/resource_util.h"
#include "pagespeed/core/result_provider.h"
#include "pagespeed/core/rule_input.h"
#include "pagespeed/core/string_util.h"
#include "pagespeed/core/uri_util.h"
#include "pagespeed/l10n/l10n.h"
#include "pagespeed/proto/pagespeed_output.pb.h"

namespace {

const char* kRuleName = "AvoidLandingPageRedirects";
const char* kLoginSubstring = "login";

const int64 kMillisInADay = 1000 * 60 * 60 * 24;
const int64 kMillisInAWeek = kMillisInADay * 7;

const pagespeed::RedirectionDetails* GetDetails(
    const pagespeed::Result& result) {
  const pagespeed::ResultDetails& details = result.details();
  if (!details.HasExtension(
          pagespeed::RedirectionDetails::message_set_extension)) {
    LOG(DFATAL) << "RedirectionDetails missing.";
    return NULL;
  }

  return &details.GetExtension(
      pagespeed::RedirectionDetails::message_set_extension);
}


bool SortRuleResultsByRedirection(const pagespeed::Result* lhs,
                                  const pagespeed::Result* rhs) {
  const pagespeed::RedirectionDetails* lhs_details = GetDetails(*lhs);
  const pagespeed::RedirectionDetails* rhs_details = GetDetails(*rhs);
  return lhs_details->chain_index() < rhs_details->chain_index();
}

// Return the host:port pair from a GURL as a string.  If the port is not
// explicitly given, infer it from the URL scheme.
std::string GetHostAndPort(const GURL& gurl) {
  std::string host = gurl.host();
  std::string port = gurl.port();
  if (port.empty()) {
    port = (gurl.scheme() == "https" ? "443" : "80");
  }
  return host + ":" + port;
}

}  // namespace

namespace pagespeed {

namespace rules {

AvoidLandingPageRedirects::AvoidLandingPageRedirects()
    : Rule(InputCapabilities()) {
}

const char* AvoidLandingPageRedirects::name() const {
  return kRuleName;
}

UserFacingString AvoidLandingPageRedirects::header() const {
  // TRANSLATOR: The name of a Page Speed rule that tells users to avoid
  // redirects at the landing page. The landing page is the root
  // HTML document that was requested the user in the browser's address bar.
  // This is displayed in a list of rule names that Page Speed generates.
  return _("Avoid landing page redirects");
}

bool AvoidLandingPageRedirects::AppendResults(
    const RuleInput& rule_input, ResultProvider* provider) {
  const PagespeedInput& input = rule_input.pagespeed_input();
  const Resource* primary_resource =
      input.GetResourceCollection().GetPrimaryResourceOrNull();

  if (primary_resource == NULL) {
    LOG(ERROR) << "Cannot find primary resource.";
    return false;
  }
  const RedirectRegistry::RedirectChain* chain =
    input.GetResourceCollection()
    .GetRedirectRegistry()->GetRedirectChainOrNull(primary_resource);

  // When there is one redirect, the chain size is 2, as it includes both the
  // initial url and the final url.
  if (chain == NULL || chain->empty() ||
      (!input.GetInitialResourceIsCanonical() && chain->size() <= 2)) {
    return true;
  }

  if (resource_util::IsErrorResourceStatusCode(
          chain->back()->GetResponseStatusCode())) {
    // If the user was redirected to an error page, it should not be a
    // bad redirect.
    return true;
  }

  // Keep track of which hostnames we've had to do DNS lookups for so far
  // (starting with the original request URL for the page).
  std::set<std::string> hosts_used;
  const GURL request_gurl(chain->front()->GetRequestUrl());
  if (!request_gurl.HostIsIPAddress()) {
    hosts_used.insert(request_gurl.host());
  }

  // Keep track of which host:port combinations we've had to open a TCP
  // connection to (starting with the original request URL for the page).
  std::set<std::string> tcp_connections_used;
  tcp_connections_used.insert(GetHostAndPort(request_gurl));

  // All redirections should be avoided for landing page. We flag both temporary
  // and permanent redirections.
  for (int idx = 0, size = chain->size(); idx < size; ++idx) {
    const Resource* resource =  chain->at(idx);
    if (resource->GetResourceType() != REDIRECT) {
      // The last resource in each chain is the final resource, which
      // should not be considered here.
      continue;
    }
    // We want to record the redirect and its destination so we can present that
    // information in the UI.
    if (idx == size - 1) {
      continue;  // This is the last redirection.
    }

    const std::string& url = resource->GetRequestUrl();
    GURL gurl(url);
    const std::string& next_url = chain->at(idx+1)->GetRequestUrl();
    GURL next_gurl(next_url);

    // We'll have to do a new DNS lookup for the destination of this redirect
    // if next_url is not an IP address and hasn't already been looked up.
    const std::string next_host = next_gurl.host();
    const bool needed_extra_dns =
        (!next_gurl.HostIsIPAddress() && hosts_used.count(next_host) == 0);
    if (needed_extra_dns) {
      hosts_used.insert(next_host);
    }

    // We'll have to open a new TCP connection for the destination of this
    // redirect if we don't already have one open.  In addition, we may have to
    // do an SSL/TLS handshake first.
    const std::string next_host_port = GetHostAndPort(next_gurl);
    const bool needed_extra_tcp_handshake =
        (tcp_connections_used.count(next_host_port) == 0);
    if (needed_extra_tcp_handshake) {
      tcp_connections_used.insert(next_host_port);
    }
    const bool needed_extra_ssl_handshake =
        (needed_extra_tcp_handshake && next_gurl.SchemeIsSecure());

    Result* result = provider->NewResult();
    result->add_resource_urls(url);
    result->add_resource_urls(next_url);
    // We don't penalize people for the first url when we allow a single redirect.
    if (idx != 0 || input.GetInitialResourceIsCanonical()) {
      Savings* savings = result->mutable_savings();
      savings->set_requests_saved(1);
      // TODO(mdsteele): If needed_extra_dns is true, maybe we should also do
      //   savings->set_dns_requests_saved(1), but only if we won't be using that
      //   host anyway for other resources.
      // TODO(mdsteele): If needed_extra_tcp_handshake is true, maybe we should
      //   also do savings->set_connections_saved(1), but only if we won't be
      //   using that connection anyway for other resources.
      savings->set_render_blocking_round_trips_saved(
          1 + (needed_extra_dns ? 1 : 0) + (needed_extra_tcp_handshake ? 1 : 0) +
          (needed_extra_ssl_handshake ? 1 : 0));
    }

    ResultDetails* details = result->mutable_details();
    RedirectionDetails* redirection_details =
        details->MutableExtension(
            RedirectionDetails::message_set_extension);

    bool permanent_redirection = (resource->GetResponseStatusCode() == 301);

    bool cacheable = permanent_redirection;
    int64 freshness_lifetime_millis = 0;
    ResourceCacheComputer comp(resource);
    if (comp.GetFreshnessLifetimeMillis(&freshness_lifetime_millis)) {
      cacheable = freshness_lifetime_millis >= kMillisInAWeek;
      redirection_details->set_freshness_lifetime_millis(
          freshness_lifetime_millis);
      LOG(INFO) << "freshness_lifetime_millis: " << freshness_lifetime_millis;
      // An explicit cache freshness life time is specified, the redirection is
      // not permanent by any way.
      redirection_details->set_is_permanent(false);
    } else {
      redirection_details->set_is_permanent(permanent_redirection);
    }
    redirection_details->set_is_cacheable(cacheable);
    bool same_host = (gurl.host() == next_gurl.host());
    redirection_details->set_is_same_host(same_host);

    const std::string login(kLoginSubstring);
    std::string::const_iterator login_it =
    std::search(next_url.begin(), next_url.end(),
                login.begin(), login.end(),
                string_util::CaseInsensitiveCompareASCII());
    bool is_login = (login_it != next_url.end());
    redirection_details->set_is_likely_login(is_login);

    bool is_callback = (next_gurl.query().find(url) != std::string::npos);
    redirection_details->set_is_likely_callback(is_callback);

    redirection_details->set_chain_index(idx);
    redirection_details->set_chain_length(size);
  }

  return true;
}

void AvoidLandingPageRedirects::FormatResults(
    const ResultVector& results, RuleFormatter* formatter) {
  if (results.empty()) {
    // TRANSLATOR: A summary to give a general overview of this Page
    // Speed rule.
    formatter->SetSummaryLine(
        _("Your page has no redirects. Learn more about %(BEGIN_LINK)savoiding "
          "landing page redirects%(END_LINK)s."),
        HyperlinkArgument(
            "LINK",
            "https://developers.google.com/speed/docs/insights/"
            "AvoidRedirects"));
    return;
  }

  // TRANSLATOR: A summary to give a general overview of this Page
  // Speed rule.
  formatter->SetSummaryLine(
      _("Your page has %(NUM_REDIRECTS)s redirects. "
        "Redirects introduce additional delays before "
        "the page can be loaded."),
      IntArgument("NUM_REDIRECTS", results.size()));

  UrlBlockFormatter* body = formatter->AddUrlBlock(
      // TRANSLATOR: Header at the top of a list of URLs that Page
      // Speed detected as a chain of HTTP redirections. It tells the
      // user to fix the problem by removing the URLs that redirect to
      // others. The text between BEGIN_LINK and END_LINK will be
      // displayed as a clickable link in the browser, which takes the
      // user to a document providing additional information.
      _("%(BEGIN_LINK)sAvoid landing page redirects%(END_LINK)s for the "
        "following chain of redirected URLs."),
      HyperlinkArgument(
          "LINK",
          "https://developers.google.com/speed/docs/insights/AvoidRedirects"));

  // Add the very first url onto the front of the list. Then loop over
  // everything, adding on the second url from each result.
  body->AddUrlResult(
      not_localized("%(FIRST_URL)s"),
      UrlArgument("FIRST_URL", (*results.begin())->resource_urls(0)));

  for (ResultVector::const_iterator iter = results.begin(),
           end = results.end();
       iter != end;
       ++iter) {
    const Result& result = **iter;
    if (result.resource_urls_size() != 2) {
      LOG(DFATAL) << "Unexpected number of resource URLs.  Expected 2, Got "
                  << result.resource_urls_size() << ".";
      continue;
    }

    body->AddUrlResult(
        not_localized("%(REDIRECTED_URL)s"),
        UrlArgument("REDIRECTED_URL", result.resource_urls(1)));
  }
}

void AvoidLandingPageRedirects::SortResultsInPresentationOrder(
    ResultVector* rule_results) const {
  // Sort the results in request order so that the user can easily see the
  // redirection chain.
  std::stable_sort(rule_results->begin(),
                   rule_results->end(),
                   SortRuleResultsByRedirection);
}

}  // namespace rules

}  // namespace pagespeed
