/*
    Copyright 2005-2011 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#ifndef UTILITY_H_
#define UTILITY_H_
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <numeric>
#include <stdexcept>
#include <memory>
#include <cassert>

namespace utility{
    namespace internal{
        //TODO: add tcs
        template<class dest_type>
        dest_type& string_to(std::string const& s, dest_type& result){
            std::stringstream stream(s);
            stream>>result;
            if ((!stream)||(stream.fail())){
                throw std::invalid_argument("error converting string \""+std::string(s)+"\"");
            }
            return result;
        }

        template<class dest_type>
        dest_type string_to(std::string const& s){
            dest_type result;
            return string_to(s,result);
        }


        template<typename>
        struct is_bool { enum{value=false};};

        template<>
        struct is_bool<bool> { enum{value=true};};

        class type_base {
            type_base& operator=(const type_base&);
            public:
            const std::string name;
            const std::string description;

            type_base (std::string name, std::string description) : name(name), description(description) {}
            virtual void parse_and_store (const std::string & s)=0;
            virtual std::string value() const =0;
            virtual std::auto_ptr<type_base> clone()const =0;
            virtual ~type_base(){}
        };
        template <typename type>
        class type_impl : public type_base {
        private:
            type_impl& operator=(const type_impl&);
            typedef bool(*validating_function_type)(const type&);
        private:
            type & target;
            validating_function_type validating_function;
        public:
            type_impl(std::string name, std::string description, type & target, validating_function_type validating_function = NULL)
                : type_base (name,description), target(target),validating_function(validating_function)
            {};
            void parse_and_store (const std::string & s){
                try{
                    const bool is_bool_type =is_bool<type>::value;
                    if ((is_bool_type)&&(s.empty())){
                        //to avoid directly assigning true
                        //(as it will impose additional layer of indirection)
                        //so, simply pass it as string
                        internal::string_to("1",target);
                    }else {
                        internal::string_to(s,target);
                    }
                }catch(std::invalid_argument& ){
                    std::stringstream str;
                    str
                        <<"\""<<s<<"\""<<" is incorrect input for argument "
                        <<"\""<<name<<"\""
                    ;
                    throw std::invalid_argument(str.str());
                }
                if (validating_function){
                    if (!((validating_function)(target))){
                        std::stringstream str;
                        str
                            <<"\""<<target<<"\" is invalid value for argument "
                            <<"\""<<name<<"\""
                        ;
                        throw std::invalid_argument(str.str());
                    }
                }
            }
            virtual std::string value()const{
                std::stringstream str;
                str<<target;
                return str.str();
            }
            virtual std::auto_ptr<type_base> clone()const {
                return std::auto_ptr<type_base>(new type_impl(*this));
            }
        };

        class argument{
        private:
            std::auto_ptr<type_base> p_type;
            bool matched_;
        public:
            argument(argument const& other): p_type(other.p_type.get() ? other.p_type->clone():std::auto_ptr<type_base>()),matched_(other.matched_){}
            argument& operator=(argument a){
                this->swap(a);
                return *this;
            }
            void swap(argument& other){
                std::auto_ptr<type_base> tmp; tmp=p_type; p_type=other.p_type; other.p_type=tmp;
                std::swap(matched_,other.matched_);
            }
            template<class type>
            argument(std::string name, std::string description, type& dest, bool(*validating_function)(const type&)= NULL)
                :p_type(new type_impl<type>(name,description,dest,validating_function))
                 ,matched_(false)
            {}
            std::string value()const{
                return p_type->value();
            }
            std::string name()const{
                return p_type->name;
            }
            std::string description() const{
                return p_type->description;
            }
            void parse_and_store(const std::string & s){
                p_type->parse_and_store(s);
                matched_=true;
            }
            bool is_matched() const{return matched_;}
        };

    }
    class cli_argument_pack{
        typedef std::map<std::string,internal::argument> args_map_type;
        typedef std::vector<std::string> args_display_order_type;
        typedef std::vector<std::string> positional_arg_names_type;
    private:
        args_map_type args_map;
        args_display_order_type args_display_order;
        positional_arg_names_type positional_arg_names;
        std::set<std::string> bool_args_names;
    private:
        void add_arg(internal::argument const& a){
            std::pair<args_map_type::iterator, bool> result = args_map.insert(std::make_pair(a.name(),a));
            if (!result.second){
                throw std::invalid_argument("argument with name: \""+a.name()+"\" already registered");
            }
            args_display_order.push_back(a.name());
        }
    public:
        template<typename type>
        cli_argument_pack& arg(type& dest,std::string const& name, std::string const& description, bool(*validate)(const type &)= NULL){
            internal::argument a(name,description,dest,validate);
            add_arg(a);
            if (internal::is_bool<type>::value){
                bool_args_names.insert(name);
            }
            return *this;
        }

        //Positional means that argument name can be omitted in actual CL
        //only key to match values for parameters with
        template<typename type>
        cli_argument_pack& positional_arg(type& dest,std::string const& name, std::string const& description, bool(*validate)(const type &)= NULL){
            internal::argument a(name,description,dest,validate);
            add_arg(a);
            if (internal::is_bool<type>::value){
                bool_args_names.insert(name);
            }
            positional_arg_names.push_back(name);
            return *this;
        }

        void parse(int argc, char const* argv[]){
            {
                std::size_t current_positional_index=0;
                for (int j=1;j<argc;j++){
                    internal::argument* pa = NULL;
                    std::string argument_value;

                    const char * const begin=argv[j];
                    const char * const end=begin+std::strlen(argv[j]);

                    const char * const assign_sign = std::find(begin,end,'=');

                    struct throw_unknow_parameter{ static void _(std::string const& location){
                        throw std::invalid_argument(std::string("unknown parameter starting at:\"")+location+"\"");
                    }};
                    //first try to interpret it like parameter=value string
                    if (assign_sign!=end){
                        std::string name_found = std::string(begin,assign_sign);
                        args_map_type::iterator it = args_map.find(name_found );

                        if(it!=args_map.end()){
                            pa= &((*it).second);
                            argument_value = std::string(assign_sign+1,end);
                        }else {
                            throw_unknow_parameter::_(argv[j]);
                        }
                    }
                    //then see is it a named flag
                    else{
                        args_map_type::iterator it = args_map.find(argv[j] );
                        if(it!=args_map.end()){
                            pa= &((*it).second);
                            argument_value = "";
                        }
                        //then try it as positional argument without name specified
                        else if (current_positional_index < positional_arg_names.size()){
                            std::stringstream str(argv[j]);
                            args_map_type::iterator it = args_map.find(positional_arg_names.at(current_positional_index));
                            //TODO: probably use of smarter assert would help here
                            assert(it!=args_map.end()/*&&"positional_arg_names and args_map are out of sync"*/);
                            if (it==args_map.end()){
                                throw std::logic_error("positional_arg_names and args_map are out of sync");
                            }
                            pa= &((*it).second);
                            argument_value = argv[j];

                            current_positional_index++;
                        }else {
                            //TODO: add tc to check
                            throw_unknow_parameter::_(argv[j]);
                        }
                    }
                    assert(pa);
                    if (pa->is_matched()){
                        throw std::invalid_argument(std::string("several values specified for: \"")+pa->name()+"\" argument");
                    }
                    pa->parse_and_store(argument_value);
                }
            }
        }
        std::string usage_string(const std::string& binary_name)const{
            std::string command_line_params;
            std::string summary_description;

            for (args_display_order_type::const_iterator it = args_display_order.begin();it!=args_display_order.end();++it){
                const bool is_bool = (0!=bool_args_names.count((*it)));
                args_map_type::const_iterator argument_it = args_map.find(*it);
                //TODO: probably use of smarter assert would help here
                assert(argument_it!=args_map.end()/*&&"args_display_order and args_map are out of sync"*/);
                if (argument_it==args_map.end()){
                    throw std::logic_error("args_display_order and args_map are out of sync");
                }
                const internal::argument & a = (*argument_it).second;
                command_line_params +=" [" + a.name() + (is_bool ?"":"=value")+ "]";
                summary_description +=" " + a.name() + " - " + a.description() +" ("+a.value() +")" + "\n";
            }

            std::string positional_arg_cl;
            for (positional_arg_names_type::const_iterator it = positional_arg_names.begin();it!=positional_arg_names.end();++it){
                positional_arg_cl +=" ["+(*it);
            }
            for (std::size_t i=0;i<positional_arg_names.size();++i){
                positional_arg_cl+="]";
            }
            command_line_params+=positional_arg_cl;
            std::stringstream str;
            using std::endl;
            str << " Program usage is:" << endl
                 << " " << binary_name << command_line_params
                 << endl << endl
                 << " where:" << endl
                 << summary_description
            ;
            return str.str();
        }
    };
}

namespace utility{
    struct thread_number_range{
        int (*auto_number_of_threads)();
        int first;
        int last;

        thread_number_range( int (*auto_number_of_threads_)(),int low_=1, int high_=-1 )
            : auto_number_of_threads(auto_number_of_threads_), first(low_), last((high_>-1) ? high_ : auto_number_of_threads_())
        {
            if (first>last){
                throw std::invalid_argument("");
            }
        }
        friend std::istream& operator>>(std::istream& i, thread_number_range& range){
            try{
                std::string s;
                i>>s;
                struct string_to_number_of_threads{
                    int auto_value;
                    string_to_number_of_threads(int auto_value_):auto_value(auto_value_){}
                    int operator()(const std::string & value)const{
                        int result=0;
                        if (value=="auto"){
                            result = auto_value;
                        }
                        else{
                            internal::string_to(value,result);
                        }
                        return result;
                    }
                };
                string_to_number_of_threads string_to_number_of_threads(range.auto_number_of_threads());
                int low =0;
                int high=0;
                std::size_t semicolon = s.find(':');
                if (semicolon == std::string::npos ){
                    high= (low = string_to_number_of_threads(s));
                }else {
                    //it is a range
                    std::string::iterator semicolon_it = s.begin()+semicolon;
                    low  = string_to_number_of_threads(std::string(s.begin(),semicolon_it));
                    high = string_to_number_of_threads(std::string(semicolon_it+1,s.end()));
                }
                range = thread_number_range(range.auto_number_of_threads,low,high);
            }catch(std::invalid_argument&){
                i.setstate(std::ios::failbit);
            }
            return i;
        }
        friend std::ostream& operator<<(std::ostream& o, thread_number_range const& range){
            o<<range.first<<":"<<range.last;
            return o;
        }

    };
}
#include <iostream>
namespace utility{
    inline void report_elapsed_time(double seconds){
        std::cout << "elapsed time : "<<seconds<<" seconds \n";
    }
}
#include <cstdlib>
namespace utility{
    inline void parse_cli_arguments(int argc, const char* argv[], utility::cli_argument_pack cli_pack){
        bool show_help = false;
        cli_pack.arg(show_help,"-h","show this message");

        bool invalid_input=false;
        try {
            cli_pack.parse(argc,argv);
        }catch(std::exception& e){
            std::cerr
                    <<"error occurred while parsing command line."<<std::endl
                    <<"error text:\""<<e.what()<<"\""<<std::endl
                    <<std::flush;
            invalid_input =true;
        }
        if (show_help || invalid_input){
            std::cout<<cli_pack.usage_string(argv[0])<<std::flush;
            std::exit(0);
        }

    }
    inline void parse_cli_arguments(int argc, char* argv[], utility::cli_argument_pack cli_pack){
         parse_cli_arguments(argc, const_cast<const char**>(argv), cli_pack);
    }
}
#endif /* UTILITY_H_ */
