// Copyright 2010 Google Inc. All Rights Reserved.
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

#include "pagespeed/rules/inline_small_resources.h"

#include "base/logging.h"
#include "googleurl/src/gurl.h"
#include "net/base/registry_controlled_domain.h"
#include "net/instaweb/htmlparse/public/empty_html_filter.h"
#include "net/instaweb/htmlparse/public/html_parse.h"
#include "net/instaweb/util/public/google_message_handler.h"
#include "pagespeed/core/formatter.h"
#include "pagespeed/core/pagespeed_input.h"
#include "pagespeed/core/resource.h"
#include "pagespeed/core/resource_util.h"
#include "pagespeed/core/result_provider.h"
#include "pagespeed/core/rule_input.h"
#include "pagespeed/cssmin/cssmin.h"
#include "pagespeed/html/external_resource_filter.h"
#include "pagespeed/l10n/l10n.h"
#include "pagespeed/jsminify/js_minify.h"
#include "pagespeed/proto/pagespeed_output.pb.h"

namespace pagespeed {

namespace {

// TODO: pick a better constant. Experiment. Sync with mod_pagespeed
// on a common default.
static const int kInlineThresholdBytes = 768;

} // namespace

namespace rules {

InlineSmallResources::InlineSmallResources(ResourceType resource_type)
    : pagespeed::Rule(pagespeed::InputCapabilities(
        pagespeed::InputCapabilities::LAZY_LOADED |
        pagespeed::InputCapabilities::RESPONSE_BODY)),
      resource_type_(resource_type) {
}

bool InlineSmallResources::AppendResults(const RuleInput& rule_input,
                                         ResultProvider* provider) {
  bool error = false;
  const PagespeedInput& input = rule_input.pagespeed_input();
  net_instaweb::GoogleMessageHandler message_handler;
  message_handler.set_min_message_type(net_instaweb::kError);
  net_instaweb::HtmlParse html_parse(&message_handler);
  html::ExternalResourceFilter filter(&html_parse);
  html_parse.AddFilter(&filter);

  std::map<std::string, ResourceSet> inline_candidates;
  for (int i = 0, num = input.num_resources(); i < num; ++i) {
    const Resource& resource = input.GetResource(i);
    if (resource.IsLazyLoaded(input)) {
      continue;
    }

    if (resource.GetResourceType() != HTML) {
      continue;
    }

    html_parse.StartParse(resource.GetRequestUrl().c_str());
    html_parse.ParseText(
        resource.GetResponseBody().data(), resource.GetResponseBody().length());
    html_parse.FinishParse();

    std::vector<std::string> external_resource_urls;
    if (!filter.GetExternalResourceUrls(&external_resource_urls,
                                        input.dom_document(),
                                        resource.GetRequestUrl())) {
      continue;
    }

    GURL resource_gurl(resource.GetRequestUrl());
    std::string resource_domain =
        net::RegistryControlledDomainService::GetDomainAndRegistry(
            resource_gurl);
    if (resource_domain.empty()) {
      LOG(INFO) << "Got empty domain for " << resource.GetRequestUrl();
      continue;
    }

    for (std::vector<std::string>::const_iterator
             it = external_resource_urls.begin(),
             end = external_resource_urls.end();
         it != end;
         ++it) {
      const Resource* external_resource = input.GetResourceWithUrl(*it);
      if (IsInlineCandidate(external_resource, resource_domain)) {
        inline_candidates[resource.GetRequestUrl()].insert(external_resource);
      }
    }
  }

  for (std::map<std::string, ResourceSet>::const_iterator it =
           inline_candidates.begin(), end = inline_candidates.end();
       it != end; ++it) {
    const std::string& html_url = it->first;
    const ResourceSet& resources = it->second;

    Result* result = provider->NewResult();
    result->add_resource_urls(html_url);
    pagespeed::Savings* savings = result->mutable_savings();
    // TODO: some may be critical path requests. Consider improving
    // the statistics we gather from this rule.
    savings->set_requests_saved(resources.size());
    ResultDetails* details = result->mutable_details();
    InlineSmallResourcesDetails* isr_details =
        details->MutableExtension(
            InlineSmallResourcesDetails::message_set_extension);
    for (ResourceSet::const_iterator iter =
             resources.begin(), end = resources.end();
         iter != end; ++iter) {
      const Resource* resource = *iter;
      isr_details->add_inline_candidates(resource->GetRequestUrl());
    }
  }

  return !error;
}

// Is this resource a candidate for inlining into the HTML document?
bool InlineSmallResources::IsInlineCandidate(const Resource* resource,
                                             const std::string& html_domain) {
  if (resource == NULL) {
    return false;
  }

  if (resource->GetResourceType() != resource_type_) {
    return false;
  }

  // It's possible to inline dynamic content, but much harder (the
  // HTML generator has to know how to generate the dynamic
  // resource). Thus we don't try to recommend inlining non-static
  // resources.
  if (!resource_util::IsLikelyStaticResource(*resource)) {
    return false;
  }

  GURL resource_gurl(resource->GetRequestUrl());
  std::string resource_domain =
      net::RegistryControlledDomainService::GetDomainAndRegistry(
          resource_gurl);
  if (resource_domain.empty()) {
    LOG(INFO) << "Got empty domain for "
              << resource->GetRequestUrl();
    return false;
  }

  // Can't inline resources that are served from different
  // origins. For instance it would not make sense to inline a
  // third-party tracking script in your own content since that
  // tracking script's contents are outside of the site's control and
  // may change at any time.
  if (resource_domain != html_domain) {
    return false;
  }

  // Compute the minified size of the resource.
  const std::string& body = resource->GetResponseBody();
  int resource_size = 0;
  if (!ComputeMinifiedSize(body, &resource_size)) {
    resource_size = body.size();
  }

  return (resource_size < kInlineThresholdBytes);
}

void InlineSmallResources::FormatResults(const ResultVector& results,
                                         Formatter* formatter) {
  if (results.empty()) {
    return;
  }

  Formatter* body = formatter->AddChild(
      // TRANSLATOR: TODO: add a translator comment describing this string
      _("The following external resources have small response bodies. "
       "Inlining the response in HTML can reduce blocking "
       "of page rendering."));

  for (ResultVector::const_iterator it = results.begin(), end = results.end();
       it != end; ++it) {
    const Result& result = **it;
    if (result.resource_urls_size() != 1) {
      LOG(DFATAL) << "Unexpected number of resource URLs.  Expected 1, Got "
                  << result.resource_urls_size() << ".";
      continue;
    }

    const ResultDetails& details = result.details();
    if (details.HasExtension(
            InlineSmallResourcesDetails::message_set_extension)) {
      const InlineSmallResourcesDetails& isr_details = details.GetExtension(
          InlineSmallResourcesDetails::message_set_extension);

      Argument document_url(Argument::URL, result.resource_urls(0));
      Formatter* child =
          // TRANSLATOR: TODO: add a translator comment describing this string
          body->AddChild(_("$1 should inline the following small resources:"),
                         document_url);
      for (int i = 0; i < isr_details.inline_candidates_size(); ++i) {
        Argument candidate_url(Argument::URL, isr_details.inline_candidates(i));
        child->AddChild(not_localized("$1"), candidate_url);
      }
    } else {
      LOG(DFATAL) << "InlineSmallResourcesDetails missing.";
    }
  }
}

// Our score computation could be better. We compute the score as:
// (num_js_or_css_resources-num_inlineable_js_or_css_resources)/
//    num_js_or_css_resources
// This doesn't seem quite right, but it's not clear how to compute a
// better score for this rule. One benefit to this score computation
// is that as a site reduces the number of JS files, e.g. by combining
// them, the score will drop for this rule. So when there are many JS
// resources, this rule will not be prominent in the results list, and
// we'll likely suggest combining resources instead. Once resources
// have been combined, the score for this rule will go down, and it
// will become more prominent in the results list.
int InlineSmallResources::ComputeScore(const InputInformation& input_info,
                                       const ResultVector& results) {
  int total_resources = GetTotalResourcesOfSameType(input_info);
  if (total_resources == 0) {
    // No candidates to inline.
    return 100;
  }

  int num_candidates = 0;
  for (ResultVector::const_iterator it = results.begin(), end = results.end();
       it != end; ++it) {
    const Result& result = **it;
    const ResultDetails& details = result.details();
    if (!details.HasExtension(
            InlineSmallResourcesDetails::message_set_extension)) {
      LOG(DFATAL) << "InlineSmallResourcesDetails missing.";
      continue;
    }

    const InlineSmallResourcesDetails& isr_details =
        details.GetExtension(
            InlineSmallResourcesDetails::message_set_extension);
    DCHECK(isr_details.inline_candidates_size() > 0);
    num_candidates += isr_details.inline_candidates_size();
  }
  if (num_candidates > total_resources) {
    LOG(DFATAL) << "Number of candidates " << num_candidates
                << " exceeds total resources " << total_resources;
    return -1;
  }

  return 100 - (100 * num_candidates / total_resources);
}

InlineSmallCss::InlineSmallCss() : InlineSmallResources(CSS) {}
const char* InlineSmallCss::name() const { return "InlineSmallCss"; }
LocalizableString InlineSmallCss::header() const {
  // TRANSLATOR: TODO: add a translator comment describing this string
  return _("Inline Small CSS");
}
const char* InlineSmallCss::documentation_url() const {
  return "caching.html#InlineSmallResources";
}

bool InlineSmallCss::ComputeMinifiedSize(
    const std::string& body, int* out_minified_size) const {
  return cssmin::GetMinifiedCssSize(body, out_minified_size);
}

int InlineSmallCss::GetTotalResourcesOfSameType(
    const InputInformation& input_info) const {
  return input_info.number_css_resources();
}

InlineSmallJavaScript::InlineSmallJavaScript() : InlineSmallResources(JS) {}
const char* InlineSmallJavaScript::name() const {
  return "InlineSmallJavaScript";
}

LocalizableString InlineSmallJavaScript::header() const {
  // TRANSLATOR: TODO: add a translator comment describing this string
  return _("Inline Small JavaScript");
}

const char* InlineSmallJavaScript::documentation_url() const {
  return "caching.html#InlineSmallResources";
}

bool InlineSmallJavaScript::ComputeMinifiedSize(
    const std::string& body, int* out_minified_size) const {
  return jsminify::GetMinifiedJsSize(body, out_minified_size);
}

int InlineSmallJavaScript::GetTotalResourcesOfSameType(
    const InputInformation& input_info) const {
  return input_info.number_js_resources();
}

}  // namespace rules

}  // namespace pagespeed
