#include "query_optimizer.h"
#include "query_tree.h"
#include <iostream>


// testing yaw
// cmake --build build --target test_query_optimizer
// ./build/src/test_query_optimizer

  int main() {                                                                                                                                   
      mdbms::qo::OptimizationEngine opt;                                                                                                         
                                                                                                                                                 
      std::string q =                                                                                                                            
          "SELECT s.name, d.nama "                                                                                                               
          "FROM student s JOIN dept d ON s.dept_id = d.id JOIN apt a ON a_id = s.dept_id "                                                       
          "WHERE s.age > 20 AND d.size >= 10 AND a.name = 'bebek'";                                                                              
                                                                                                                                                 
      auto pq = opt.parse_query(q);                                                                                                              



      // DEBUG 

      std::cout << "Semua Query\n" << pq.raw_query << "\n\n";                                                                                    
             
      auto print_list = [](const char* label, const auto& list, auto formatter) {                                                                
          std::cout << label << " -> [";                                                                                                         
          for (size_t i = 0; i < list.size(); ++i) {                                                                                             
              if (i > 0) std::cout << ", ";                                                                                                      
              formatter(list[i]);                                                                                                                
          }                                                                                                                                      
          std::cout << "]\n";                                                                                                                    
      };                                                                                                                                         
                                                                                                                                                 
      print_list("SELECT", pq.select_list,                                                                                                       
                 [](const std::string& s) { std::cout << s; });                                                                                  
      print_list("FROM", pq.from_tables,                                                                                                         
                 [](const std::string& t) { std::cout << t; });                                                                                  
      print_list("JOIN", pq.joins,                                                                                                               
                 [](const mdbms::qo::ParsedQuery::JoinCondition& j) {                                                                            
                     std::cout << j.left << " = " << j.right;                                                                                    
                 });                                                                                                                             
      print_list("WHERE", pq.where_conditions,                                                                                                   
                 [](const std::string& w) { std::cout << w; });                                                                                  

      std::cout << "\nudah";                                                                                                 
      return 0;
  } 