import readline from 'readline';
import fs from 'fs';
import { color } from './chalk-colors.js';
import { regex_validator } from './sanitizer.js';

// СОЗДАЕМ ОБЪЕКТ ИНТЕРФЕЙС
const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
})



// СТАНДАРТНАЯ Ф-Я ИЗ READLINE 
async function askQuestion(query) {
    return new Promise(resolve => rl.question(query, answer => resolve(answer)));
}



// ПРОВЕРЯЕМ ШАБЛОН, ЕСЛИ ШАБЛОН НЕ ПРОЙДЕН, СПРАШИВАЕМ ТОТ ЖЕ ПУНКТ
async function checkout(position_name, question) {
    while (true) {
        const user_input = await askQuestion(question);

        if (!regex_validator(position_name, user_input)) {
            console.log(color.warn("невалидный выбор. Попробуй еще раз!\n"));

            continue;
        }

        return user_input;
    }
}



// ПОЛУЧАЕМ ПОЛЬЗОВАТЕЛЬСКИЙ ВВОД
async function get_prompt() {
    try {
        let menu = await checkout("menu", "1. Создать промпт\n2. Отмена\n\nВыбор: ");

        if (menu == 2) {
            return {
                data_info: null,
                isAccept: 'n'
            };
        }

        let user_prompt = await checkout("user_prompt", "1.1 Введи промпт: ");
        let user_output = await checkout("user_output", "\n1.2 Введи оутпут (ответ) для нейросети: ");

        let data_info = {
            "instruction": user_prompt,
            "output": user_output,
        }

        console.log(`\n${color.out("PROMPT:") + " " + user_prompt}\n${color.out("OUTPUT:") + " " + user_output}\n\n`);
        let isAccept = await checkout("isAccept", "Подтвердить [Y/n]: ");

        return {
            data_info: data_info,
            isAccept: isAccept
        }

    } catch (error) {
        console.log(color.err("произошла ошибка сбора данных!\n"), error)
    }
}



// ОБЕРТКА
async function main() {
    try {
        let exit = false;
        let counter = 0;

        while (exit != true) {
            let result = await get_prompt();

            if (result.isAccept == 'n') {
                console.log(color.err("\nоперация отменена"));
                console.log(`${counter == 0 ? "" : "    --> ПРИМЕЧАНИЕ: ПОСЛЕДНИЕ изменения не были внесены, все предыдущие промпты были записаны"}`);

                exit = true;      //<<останавливаем цикл
                process.exit(0); //<< завершаем процесс**
            }

            await fs.appendFileSync('../memory.txt', JSON.stringify(result.data_info, null, 2) + "\n");
            console.log(color.info("данные успешно записаны!\n"))

            counter++
        }

    } catch (error) {
        console.log(color.err("произошла ошибка при создании промпта!\n"), error);
    }
}

main(); //<<запуск
