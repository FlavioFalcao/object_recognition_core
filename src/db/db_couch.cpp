/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <boost/property_tree/json_parser.hpp>

#include "db_couch.h"

object_recognition::curl::cURL_GS curl_init_cleanup;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ObjectDbCouch::ObjectDbCouch(const std::string &url)
    :
      url_(url),
      json_writer_(json_writer_stream_),
      json_reader_(json_reader_stream_)
{
}

void
ObjectDbCouch::insert_object(const CollectionName &collection, const boost::property_tree::ptree &fields,
                             DocumentId & document_id, RevisionId & revision_id)
{
  upload_json(fields, url_id(collection, ""), "POST");
  GetObjectRevisionId(document_id, revision_id);
}

void
ObjectDbCouch::persist_fields(const DocumentId & document_id, const CollectionName &collection,
                              const boost::property_tree::ptree &fields, RevisionId & revision_id)
{
  precondition_id(document_id);
  upload_json(fields, url_id(collection, document_id), "PUT");
  //need to update the revision here.
  GetRevisionId(revision_id);
}

void
ObjectDbCouch::load_fields(const DocumentId & document_id, const CollectionName &collection,
                           boost::property_tree::ptree &fields)
{
  precondition_id(document_id);
  curl_.reset();
  json_writer_stream_.str("");
  curl_.setWriter(&json_writer_);

  curl_.setURL(url_id(collection, document_id));
  curl_.GET();

  curl_.perform();

  if (curl_.get_response_code() == object_recognition::curl::cURL::OK)
  {
    //update the object from the result.
    json_writer_stream_.seekg(0);
    boost::property_tree::read_json(json_writer_stream_, fields);
  }
}

void
ObjectDbCouch::set_attachment_stream(const DocumentId & document_id, const CollectionName &collection,
                                     const AttachmentName& attachment_name, const MimeType& mime_type,
                                     const std::istream& stream, RevisionId & revision_id)
{
  precondition_id(document_id);
  precondition_rev(revision_id);

  object_recognition::curl::reader binary_reader(stream);
  curl_.reset();
  curl_.setReader(&binary_reader);
  json_writer_stream_.str("");
  curl_.setWriter(&json_writer_);
  curl_.setHeader("Content-Type: " + mime_type);
  curl_.setURL(url_id(collection, document_id) + "/" + attachment_name + "?rev=" + revision_id);
  curl_.PUT();
  curl_.perform();
  GetRevisionId(revision_id);
}

void
ObjectDbCouch::get_attachment_stream(const DocumentId & document_id, const CollectionName &collection,
                                     const std::string& attachment_name, const std::string& content_type,
                                     std::ostream& stream, RevisionId & revision_id)
{
  object_recognition::curl::writer binary_writer(stream);
  curl_.reset();
  json_writer_stream_.str("");
  curl_.setWriter(&binary_writer);
  curl_.setURL(url_id(collection, document_id) + "/" + attachment_name);
  curl_.GET();
  curl_.perform();
}

void
ObjectDbCouch::GetObjectRevisionId(DocumentId& document_id, RevisionId & revision_id)
{
  boost::property_tree::ptree params;
  boost::property_tree::read_json(json_writer_stream_, params);
  document_id = params.get<std::string>("id", "");
  revision_id = params.get<std::string>("rev", "");
  std::cout << "rev: " << revision_id << std::endl;
  if (document_id.empty())
    throw std::runtime_error("Could not find the object id");
  if (revision_id.empty())
    throw std::runtime_error("Could not find the revision number");
}

void
ObjectDbCouch::GetRevisionId(RevisionId & revision_id)
{
  boost::property_tree::ptree params;
  boost::property_tree::read_json(json_writer_stream_, params);
  revision_id = params.get<std::string>("rev", "");
  if (revision_id.empty())
    throw std::runtime_error("Could not find the revision number, from GetRevisionId");
}

void
ObjectDbCouch::Query(const std::vector<std::string> & queries, const CollectionName & collection_name, int limit_rows,
                     int start_offset, int& total_rows, int& offset, std::vector<DocumentId> & document_ids)
{
  if (limit_rows <= 0)
    limit_rows = std::numeric_limits<int>::max();
  json_spirit::Object obj;
  typedef std::pair<std::string, std::string> value;
  /*BOOST_FOREACH(const value& p, v.map)
      {
        obj.push_back(json_spirit::Pair("map", p.second));
      }
  std::stringstream stream;
  json_spirit::write(obj, stream);
  object_recognition::curl::reader r(stream);
  stream_.str("");
  curl_.reset();
  curl_.setReader(&r);
  curl_.setWriter(&json_writer_);
  curl_.setURL(
      url_ + "/_temp_view?limit=" + boost::lexical_cast<std::string>(limit_rows) + "&skip="
      + boost::lexical_cast<std::string>(start_offset));
  curl_.setHeader("Content-Type: application/json");
  curl_.setCustomRequest("POST");
  curl_.perform();
  json_spirit::Value val;
  get(val);
  std::map<std::string, json_spirit::Value> result_map;
  json_spirit::obj_to_map(val.get_obj(), result_map);
  total_rows = result_map["total_rows"].get_int();
  offset = result_map["offset"].get_int();
  std::vector<json_spirit::Value> rows = result_map["rows"].get_array();
  std::vector<View::result> results;
  results.reserve(rows.size());
  BOOST_FOREACH(const json_spirit::Value& v, rows)
      {
        std::map<std::string, json_spirit::Value> row_map;
        json_spirit::obj_to_map(v.get_obj(), row_map);
        View::result r =
        { row_map["id"].get_str(), json_spirit::write(row_map["key"]), json_spirit::write(row_map["value"]) };
        results.push_back(r);
      }
  return results;*/
}

void
ObjectDbCouch::upload_json(const boost::property_tree::ptree &ptree, const std::string& url, const std::string& request)
{
  curl_.reset();
  json_writer_stream_.str("");
  json_reader_stream_.str("");
  boost::property_tree::write_json(json_reader_stream_, ptree);
  curl_.setWriter(&json_writer_);
  curl_.setReader(&json_reader_);
  //couch db post to the db
  curl_.setURL(url);
  std::cout << url << std::endl;
  curl_.setHeader("Content-Type: application/json");
  if (request == "PUT")
  {
    curl_.PUT();
  }
  else
  {
    curl_.setCustomRequest(request.c_str());
  }
  curl_.perform();
}