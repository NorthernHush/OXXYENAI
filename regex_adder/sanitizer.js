export function regex_validator(position_name, user_input) {
    const regEx = {
        menu: /^[12]$/,
        user_prompt: /^[\s\S]*$/,
        user_output: /^[\s\S]*$/,
        isAccept: /^[yn]$/im,
    }

    return regEx[position_name].test(user_input);
}